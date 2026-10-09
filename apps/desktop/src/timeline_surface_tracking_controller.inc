// 2D surface data shares the perspective attachment renderer with advanced 3D.
bool TimelineDocumentController::surfaceTrackingAvailable() const { return timelineSurfaceAvailable(); }
bool TimelineDocumentController::analyzeSelectedClipSurface(const QVariantMap& seed) {
  return startTrackingAnalysis(seed, false, false, false, true);
}
bool TimelineDocumentController::surfaceTrackDraftReady(const Clip& clip) const {
  const auto data = clip.motion_tracking.value(QStringLiteral("surfaceTrackDraft")).toMap();
  const auto fps = data.value(QStringLiteral("fps")).toDouble();
  const auto start = data.value(QStringLiteral("trimStart")).toDouble();
  const auto end = data.value(QStringLiteral("trimEnd")).toDouble();
  const auto count = data.value(QStringLiteral("frameCount")).toInt();
  const auto* track = trackAt(clipRefAt(flattenedIndexForClipId(clip.clip_id)).track_index);
  return track && sceneAttachmentGeometrySupported(clip, *track) &&
      data.value(QStringLiteral("sourceSignature")).toString() == clip.source_identity_signature &&
      count >= 2 && count <= 900 && data.value(QStringLiteral("previewFrames")).toList().size() == count &&
      std::isfinite(fps) && fps >= 1 && fps <= 30 && std::isfinite(start) && std::isfinite(end) &&
      start >= 0 && end > start && clip.trim_start_seconds >= start - kBoundaryEpsilon &&
      clip.trim_end_seconds <= end + kBoundaryEpsilon;
}
bool TimelineDocumentController::surfaceTrackMetadataReady(const Clip& clip) const {
  const auto data = clip.motion_tracking.value(QStringLiteral("surfaceTrack")).toMap();
  const auto* track = trackAt(clipRefAt(flattenedIndexForClipId(clip.clip_id)).track_index);
  const auto samples = data.value(QStringLiteral("samples")).toList();
  const auto fps = data.value(QStringLiteral("fps")).toDouble();
  if (!track || !sceneAttachmentGeometrySupported(clip, *track) || clip.playback_rate <= 0 ||
      data.value(QStringLiteral("sourceSignature")).toString() != clip.source_identity_signature ||
      data.value(QStringLiteral("solveId")).toString().isEmpty() || data.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      data.value(QStringLiteral("width")).toInt() < 2 || data.value(QStringLiteral("height")).toInt() < 2 ||
      data.value(QStringLiteral("width")).toInt() > 960 || data.value(QStringLiteral("height")).toInt() > 960 ||
      samples.size() < 2 || samples.size() > 900 || samples.size() != data.value(QStringLiteral("frameCount")).toInt() ||
      !std::isfinite(fps) || fps < 1 || fps > 30 ||
      clip.trim_start_seconds < data.value(QStringLiteral("trimStart")).toDouble() - kBoundaryEpsilon ||
      clip.trim_end_seconds > data.value(QStringLiteral("trimEnd")).toDouble() + kBoundaryEpsilon) return false;
  double previous = -1;
  for (const auto& value : samples) {
    const auto sample = value.toMap();
    const auto time = sample.value(QStringLiteral("sourceSeconds")).toDouble();
    const auto confidence = sample.value(QStringLiteral("confidence"), -1).toDouble();
    const auto quad = sample.value(QStringLiteral("quad")).toList();
    if (!std::isfinite(time) || time <= previous || quad.size() != 4 || !std::isfinite(confidence) || confidence < 0.3 || confidence > 1 ||
        sample.value(QStringLiteral("inliers")).toInt() < 10 ||
        !std::isfinite(sample.value(QStringLiteral("errorPx"), -1).toDouble()) ||
        sample.value(QStringLiteral("errorPx"), -1).toDouble() < 0 || sample.value(QStringLiteral("errorPx")).toDouble() > 3) return false;
    previous = time;
    QPolygonF polygon;
    for (int index : {0, 1, 3, 2}) {
      const auto corner = quad[index].toMap();
      const auto x = corner.value(QStringLiteral("x"), std::numeric_limits<double>::quiet_NaN()).toDouble();
      const auto y = corner.value(QStringLiteral("y"), std::numeric_limits<double>::quiet_NaN()).toDouble();
      if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 8 || qAbs(y) > 8) return false;
      polygon.append(QPointF(x, y));
    }
    for (int index = 0; index < 4; ++index) {
      const auto a = polygon[index], b = polygon[(index + 1) % 4], c = polygon[(index + 2) % 4];
      if ((b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x()) <= 0) return false;
    }
    QTransform transform;
    if (!QTransform::quadToQuad(QPolygonF{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}, polygon, transform)) return false;
  }
  const auto first = samples.first().toMap().value(QStringLiteral("sourceSeconds")).toDouble();
  const auto last = samples.last().toMap().value(QStringLiteral("sourceSeconds")).toDouble();
  return qAbs(first - data.value(QStringLiteral("trimStart")).toDouble()) < 0.000002 &&
      qAbs(last - (first + (samples.size() - 1) / fps)) < 0.000002;
}
QVariantList TimelineDocumentController::surfaceTrackSources() const {
  QVariantList rows;
  for (const auto& track : tracks_) for (const auto& clip : track.clips) {
    if (track.kind != TrackKind::Video || !surfaceTrackMetadataReady(clip)) continue;
    const auto data = clip.motion_tracking.value(QStringLiteral("surfaceTrack")).toMap();
    const auto samples = data.value(QStringLiteral("samples")).toList();
    double minimum = 1, error = 0;
    for (const auto& value : samples) {
      const auto sample = value.toMap();
      minimum = qMin(minimum, sample.value(QStringLiteral("confidence")).toDouble());
      error += sample.value(QStringLiteral("errorPx")).toDouble();
    }
    rows.append(QVariantMap{{QStringLiteral("clipId"), clip.clip_id}, {QStringLiteral("displayLabel"), clip.display_label},
        {QStringLiteral("filePath"), clip.file_path}, {QStringLiteral("trimStartSeconds"), data.value(QStringLiteral("trimStart"))},
        {QStringLiteral("minConfidence"), minimum}, {QStringLiteral("meanErrorPx"), error / samples.size()},
        {QStringLiteral("sampleCount"), samples.size()},
        {QStringLiteral("occlusionAvailable"), timelineForegroundCutoutReady(clip.foreground_cutout, clip.source_identity_signature, clip.trim_start_seconds, clip.trim_end_seconds)}});
  }
  return rows;
}
QVariantList TimelineDocumentController::bakeSurfaceSamples(const Clip& source, const QVariantMap& placement, QString* error) const {
  if (!surfaceTrackMetadataReady(source)) { if (error) *error = QStringLiteral("Surface tracking data is stale. Track the shot again."); return {}; }
  const auto data = source.motion_tracking.value(QStringLiteral("surfaceTrack")).toMap();
  const auto aspect = static_cast<double>(data.value(QStringLiteral("width")).toInt()) / qMax(1, data.value(QStringLiteral("height")).toInt());
  const auto sequence_aspect = static_cast<double>(sequence_size_.width()) / sequence_size_.height();
  const auto ratio_x = qMin(1.0, aspect / sequence_aspect), ratio_y = qMin(1.0, sequence_aspect / aspect);
  const auto scale = placement.value(QStringLiteral("widthScale"), 1).toDouble();
  const auto dx = placement.value(QStringLiteral("offsetX")).toDouble(), dy = placement.value(QStringLiteral("offsetY")).toDouble();
  const auto angle = placement.value(QStringLiteral("rotationDegrees")).toDouble() * std::numbers::pi / 180;
  QVariantList result;
  for (const auto& value : data.value(QStringLiteral("samples")).toList()) {
    const auto sample = value.toMap();
    QPolygonF destination;
    const auto corners = sample.value(QStringLiteral("quad")).toList();
    for (int index : {0, 1, 3, 2}) { const auto p = corners[index].toMap(); destination.append(QPointF(p.value(QStringLiteral("x")).toDouble(), p.value(QStringLiteral("y")).toDouble())); }
    QTransform transform;
    if (!QTransform::quadToQuad(QPolygonF{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}, destination, transform)) return {};
    QVariantList quad;
    for (const auto& p : {QPointF(-0.5, -0.5), QPointF(0.5, -0.5), QPointF(-0.5, 0.5), QPointF(0.5, 0.5)}) {
      const auto local = QPointF(0.5 + dx + scale * (p.x() * std::cos(angle) - p.y() * std::sin(angle)),
          0.5 + dy + scale * (p.x() * std::sin(angle) + p.y() * std::cos(angle)));
      const auto mapped = transform.map(local);
      const auto x = 0.5 + (mapped.x() - 0.5) * ratio_x, y = 0.5 + (mapped.y() - 0.5) * ratio_y;
      if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 8 || qAbs(y) > 8) {
        if (error) *error = QStringLiteral("Surface placement leaves the supported projection range."); return {};
      }
      quad.append(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
    }
    result.append(QVariantMap{{QStringLiteral("sequenceSeconds"), source.start_seconds +
        (sample.value(QStringLiteral("sourceSeconds")).toDouble() - source.trim_start_seconds) / source.playback_rate}, {QStringLiteral("quad"), quad}});
  }
  return result;
}
bool TimelineDocumentController::attachSurfaceTrackToSelectedClip(const QString& source_clip_id) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto reject = [this](const QString& reason) { tracking_status_ = reason; emit trackingChanged(); setStatus(reason); return false; };
  auto* target = clipAt(selectedClipRef());
  const auto* track = trackAt(selectedClipRef().track_index);
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(source_clip_id)));
  if (!target || !track || track->locked || track->kind != TrackKind::Video || target == source ||
      (target->media_type != QStringLiteral("title") && target->media_type != QStringLiteral("caption") && target->media_type != QStringLiteral("photo")))
    return reject(QStringLiteral("Select an unlocked title, caption, or image to attach to the surface."));
  if (!source || !surfaceTrackMetadataReady(*source) || media_identity_signature(source->file_path) != source->source_identity_signature)
    return reject(QStringLiteral("The surface track is missing or stale. Track the shot again."));
  if (!sceneAttachmentGeometrySupported(*target, *track) || qAbs(target->playback_rate - 1) > kBoundaryEpsilon)
    return reject(QStringLiteral("Reset target and group transforms before attaching; use Surface Placement afterward."));
  const auto data = source->motion_tracking.value(QStringLiteral("surfaceTrack")).toMap();
  QVariantMap attachment{{QStringLiteral("mode"), QStringLiteral("surface2d")},
      {QStringLiteral("sourceClipId"), source_clip_id}, {QStringLiteral("sourceSignature"), source->source_identity_signature},
      {QStringLiteral("solveId"), data.value(QStringLiteral("solveId"))}, {QStringLiteral("sourceStart"), source->start_seconds},
      {QStringLiteral("sourceTrimStart"), source->trim_start_seconds}, {QStringLiteral("sourceRate"), source->playback_rate},
      {QStringLiteral("sequenceWidth"), sequence_size_.width()}, {QStringLiteral("sequenceHeight"), sequence_size_.height()},
      {QStringLiteral("widthScale"), 1.0}, {QStringLiteral("offsetX"), 0.0}, {QStringLiteral("offsetY"), 0.0},
      {QStringLiteral("offsetZ"), 0.0}, {QStringLiteral("rotationDegrees"), 0.0}};
  QString error;
  const auto samples = bakeSurfaceSamples(*source, attachment, &error);
  if (samples.isEmpty()) return reject(error);
  attachment.insert(QStringLiteral("samples"), samples);
  pushUndoSnapshot(); target->motion_tracking.insert(QStringLiteral("sceneAttachment"), attachment);
  target->start_seconds = source->start_seconds;
  target->trim_end_seconds = target->trim_start_seconds + clipTrimmedDuration(*source);
  target->duration_seconds = qMax(target->duration_seconds, target->trim_end_seconds);
  emitAllState(); setDirty(true);
  tracking_status_ = QStringLiteral("Layer attached to the tracked surface. Inspect perspective and occlusion through the shot.");
  emit trackingChanged(); setStatus(tracking_status_); return true;
}

bool TimelineDocumentController::correctSelectedSurfaceTrack(const QVariantList& quad) {
  const auto* clip = clipAt(selectedClipRef());
  if (!clip || (!surfaceTrackMetadataReady(*clip) && !surfaceTrackDraftReady(*clip)) || quad.size() != 4 || clip != clipAt(programClipRef())) return false;
  const auto preview = scenePreviewFrame();
  if (!preview.value(QStringLiteral("active")).toBool() || preview.value(QStringLiteral("path")).toString().isEmpty()) return false;
  const auto data = clip->motion_tracking.value(surfaceTrackDraftReady(*clip) ? QStringLiteral("surfaceTrackDraft") : QStringLiteral("surfaceTrack")).toMap();
  auto seed = data.value(QStringLiteral("seed")).toMap();
  const auto index = qRound((preview.value(QStringLiteral("sourceSeconds")).toDouble() - data.value(QStringLiteral("trimStart")).toDouble()) * data.value(QStringLiteral("fps")).toDouble());
  if (index < 0 || index >= data.value(QStringLiteral("frameCount")).toInt()) return false;
  QVariantList corrections;
  for (const auto& value : seed.value(QStringLiteral("correctionFrames")).toList())
    if (value.toMap().value(QStringLiteral("frameIndex")).toInt() != index) corrections.append(value);
  corrections.append(QVariantMap{{QStringLiteral("frameIndex"), index}, {QStringLiteral("quad"), quad}});
  seed.insert(QStringLiteral("correctionFrames"), corrections);
  return analyzeSelectedClipSurface(seed);
}
