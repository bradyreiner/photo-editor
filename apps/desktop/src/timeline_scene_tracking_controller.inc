// Camera solves remain source-time data. Attachment samples are projected once
// when attaching/editing the plane, so playback does not solve 3D geometry.
bool TimelineDocumentController::sceneTrackingAvailable() const {
  const auto configured = qEnvironmentVariable("PHOTO_EDITOR_COLMAP_BINARY").trimmed();
  if (!configured.isEmpty()) {
    const auto info = QFileInfo(configured);
    return info.isFile() && info.isExecutable();
  }
  auto executable = QStandardPaths::findExecutable(QStringLiteral("colmap"));
#ifdef Q_OS_MACOS
  if (executable.isEmpty()) {
    executable = QStandardPaths::findExecutable(
        QStringLiteral("colmap"), {QStringLiteral("/opt/homebrew/bin"), QStringLiteral("/usr/local/bin")});
  }
#endif
  return !executable.isEmpty();
}

bool TimelineDocumentController::analyzeSelectedClipSceneCamera() {
  return startTrackingAnalysis({}, false, false, true);
}

bool TimelineDocumentController::sceneAttachmentGeometrySupported(const Clip& clip, const Track& track) const {
  const auto sequence_raster = clip.media_type == QStringLiteral("title") || clip.media_type == QStringLiteral("caption");
  if ((!sequence_raster && clip.frame_mode != QStringLiteral("fit")) || qAbs(clip.frame_scale - 1) > kBoundaryEpsilon ||
      qAbs(clip.frame_offset_x) > kBoundaryEpsilon || qAbs(clip.frame_offset_y) > kBoundaryEpsilon ||
      qAbs(clip.frame_rotation_degrees) > kBoundaryEpsilon || qAbs(clip.frame_perspective_x) > kBoundaryEpsilon ||
      qAbs(clip.frame_perspective_y) > kBoundaryEpsilon ||
      clip.stabilization_enabled || clip.rolling_shutter_enabled || clipHasRetimeCurve(clip)) return false;
  // Saving materializes the active output variant even when its framing is
  // identical to the clip. Permit those identity records on reopen, while
  // rejecting actual alternative framing or animation.
  for (const auto& variant : clip.output_variant_overrides) {
    if ((!sequence_raster && variant.frame_mode != QStringLiteral("fit")) ||
        qAbs(variant.frame_scale - 1) > kBoundaryEpsilon || qAbs(variant.frame_offset_x) > kBoundaryEpsilon ||
        qAbs(variant.frame_offset_y) > kBoundaryEpsilon || qAbs(variant.frame_rotation_degrees) > kBoundaryEpsilon ||
        qAbs(variant.frame_perspective_x) > kBoundaryEpsilon || qAbs(variant.frame_perspective_y) > kBoundaryEpsilon ||
        !variant.framing_keyframes.isEmpty()) return false;
  }
  for (const auto& parameter : {QStringLiteral("frameScale"), QStringLiteral("frameOffsetX"), QStringLiteral("frameOffsetY"),
      QStringLiteral("frameRotationDegrees"), QStringLiteral("framePerspectiveX"), QStringLiteral("framePerspectiveY")})
    if (clipHasKeyframesFor(clip, parameter)) return false;
  const auto group = effectiveTrackGroupTransform(track);
  return qAbs(group.value(QStringLiteral("scale"), 1.0).toDouble() - 1) <= kBoundaryEpsilon &&
      qAbs(group.value(QStringLiteral("rotationDegrees")).toDouble()) <= kBoundaryEpsilon &&
      qAbs(group.value(QStringLiteral("offsetX")).toDouble()) <= kBoundaryEpsilon &&
      qAbs(group.value(QStringLiteral("offsetY")).toDouble()) <= kBoundaryEpsilon;
}

bool TimelineDocumentController::sceneTrackMetadataReady(const Clip& clip) const {
  const auto metadata = clip.motion_tracking.value(QStringLiteral("sceneCamera")).toMap();
  const auto fps = metadata.value(QStringLiteral("fps")).toDouble();
  const auto start = metadata.value(QStringLiteral("trimStart")).toDouble();
  const auto end = metadata.value(QStringLiteral("trimEnd")).toDouble();
  const auto* track = trackAt(clipRefAt(flattenedIndexForClipId(clip.clip_id)).track_index);
  return track && sceneAttachmentGeometrySupported(clip, *track) &&
      metadata.value(QStringLiteral("sourceSignature")).toString() == clip.source_identity_signature &&
      !metadata.value(QStringLiteral("solveId")).toString().isEmpty() &&
      std::isfinite(fps) && fps >= 1 && fps <= 30 && std::isfinite(start) && std::isfinite(end) &&
      start >= 0 && end > start && clip.trim_start_seconds >= start - kBoundaryEpsilon &&
      clip.trim_end_seconds <= end + kBoundaryEpsilon &&
      metadata.value(QStringLiteral("scene")).toMap().value(QStringLiteral("frames")).toList().size() >= 8;
}

QVariantMap TimelineDocumentController::scenePreviewFrame() const {
  const auto* source = clipAt(programClipRef());
  if (!source || source->media_type != QStringLiteral("video")) return {};
  const auto roto_ready = timelineForegroundCutoutReady(source->foreground_cutout, source->source_identity_signature,
      source->trim_start_seconds, source->trim_end_seconds) && !source->foreground_cutout.value(QStringLiteral("previewFrames")).toList().isEmpty();
  if (!surfaceTrackDraftReady(*source) && !surfaceTrackMetadataReady(*source) && !sceneTrackMetadataReady(*source) && !roto_ready) return {};
  const auto metadata = surfaceTrackDraftReady(*source) ? source->motion_tracking.value(QStringLiteral("surfaceTrackDraft")).toMap()
      : surfaceTrackMetadataReady(*source) ? source->motion_tracking.value(QStringLiteral("surfaceTrack")).toMap()
      : sceneTrackMetadataReady(*source) ? source->motion_tracking.value(QStringLiteral("sceneCamera")).toMap() : source->foreground_cutout;
  const auto frames = metadata.value(QStringLiteral("previewFrames")).toList();
  const auto source_seconds = currentClipPlayheadSeconds();
  QVariantMap selected;
  for (const auto& value : frames) {
    const auto frame = value.toMap();
    const auto time = frame.value(QStringLiteral("sourceSeconds")).toDouble();
    if (!std::isfinite(time)) continue;
    if (selected.isEmpty() || time <= source_seconds + 0.000001) selected = frame;
    if (time > source_seconds + 0.000001) break;
  }
  selected.insert(QStringLiteral("active"), true);
  selected.insert(QStringLiteral("sourceClipId"), source->clip_id);
  if (selected.value(QStringLiteral("path")).toString().isEmpty() ||
      !QFileInfo::exists(selected.value(QStringLiteral("path")).toString())) {
    selected.remove(QStringLiteral("path"));
    selected.insert(QStringLiteral("error"), QStringLiteral("Scene preview frames are unavailable. Solve this shot again to rebuild the local frame cache."));
  }
  return selected;
}

QVariantList TimelineDocumentController::sceneTrackSources() const {
  QVariantList rows;
  for (const auto& track : tracks_) {
    if (track.kind != TrackKind::Video) continue;
    for (const auto& clip : track.clips) {
      if (!sceneTrackMetadataReady(clip)) continue;
      const auto metadata = clip.motion_tracking.value(QStringLiteral("sceneCamera")).toMap();
      const auto scene = metadata.value(QStringLiteral("scene")).toMap();
      rows.append(QVariantMap{{QStringLiteral("clipId"), clip.clip_id}, {QStringLiteral("label"), clip.display_label},
          {QStringLiteral("displayLabel"), clip.display_label}, {QStringLiteral("filePath"), clip.file_path},
          {QStringLiteral("trimStartSeconds"), metadata.value(QStringLiteral("trimStart"))},
          {QStringLiteral("points"), metadata.value(QStringLiteral("firstFramePoints"))},
          {QStringLiteral("pointCount"), scene.value(QStringLiteral("points")).toList().size()},
          {QStringLiteral("error"), QString{}},
          {QStringLiteral("occlusionAvailable"), timelineForegroundCutoutReady(clip.foreground_cutout, clip.source_identity_signature, clip.trim_start_seconds, clip.trim_end_seconds)},
          {QStringLiteral("reprojectionError"), scene.value(QStringLiteral("reprojectionError"))}});
    }
  }
  return rows;
}

QVariantMap TimelineDocumentController::sceneTrackPointFrame(const QString& source_clip_id, double sequence_seconds) const {
  auto reject = [](const QString& message) -> QVariantMap {
    return {{QStringLiteral("success"), false}, {QStringLiteral("message"), message}};
  };
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(source_clip_id)));
  if (!source || !sceneTrackMetadataReady(*source) || media_identity_signature(source->file_path) != source->source_identity_signature)
    return reject(QStringLiteral("The scene solve is missing or stale. Solve this source again."));
  if (!std::isfinite(sequence_seconds) || sequence_seconds < source->start_seconds ||
      sequence_seconds >= source->start_seconds + clipTrimmedDuration(*source))
    return reject(QStringLiteral("Pause within the solved source shot to select its scene points."));
  const auto data = source->motion_tracking.value(QStringLiteral("sceneCamera")).toMap();
  const auto frames = data.value(QStringLiteral("previewFrames")).toList();
  const auto time = clipLocalSecondsForSequenceTime(*source, sequence_seconds);
  QVariantMap frame;
  for (const auto& value : frames) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("sourceSeconds")).toDouble() > time + 0.000001) break;
    frame = candidate;
  }
  const auto path = frame.value(QStringLiteral("path")).toString();
  if (frame.isEmpty() || !QFileInfo(path).isReadable())
    return reject(QStringLiteral("The exact scene frame is unavailable. Solve this source again."));
  const auto scene = data.value(QStringLiteral("scene")).toMap();
  QString error;
  QVariantList points;
  for (const auto& value : timelineSceneVisiblePoints(scene, frame.value(QStringLiteral("sourceSeconds")).toDouble(), &error)) {
    const auto point = value.toMap();
    const auto position = point.value(QStringLiteral("source")).toList();
    if (position.size() == 2) points.append(QVariantMap{{QStringLiteral("id"), point.value(QStringLiteral("id"))},
        {QStringLiteral("x"), position[0]}, {QStringLiteral("y"), position[1]}, {QStringLiteral("error"), point.value(QStringLiteral("error"))}});
  }
  if (!error.isEmpty() || points.size() < 3)
    return reject(error.isEmpty() ? QStringLiteral("This frame has fewer than three reconstructed points. Choose another frame.") : error);
  return {{QStringLiteral("success"), true}, {QStringLiteral("points"), points},
      {QStringLiteral("path"), QUrl::fromLocalFile(path).toString()},
      {QStringLiteral("sourceSeconds"), frame.value(QStringLiteral("sourceSeconds"))},
      {QStringLiteral("width"), scene.value(QStringLiteral("width"))}, {QStringLiteral("height"), scene.value(QStringLiteral("height"))}};
}

QVariantMap TimelineDocumentController::sceneAttachmentOcclusionFrame(const QString& clip_id, double sequence_seconds) const {
  const auto* clip = clipAt(clipRefAt(flattenedIndexForClipId(clip_id)));
  const auto attachment = clip ? clip->motion_tracking.value(QStringLiteral("sceneAttachment")).toMap() : QVariantMap{};
  if (!attachment.value(QStringLiteral("occlusionEnabled")).toBool()) return {{QStringLiteral("enabled"), false}};
  auto reject = [](const QString& error) -> QVariantMap {
    return {{QStringLiteral("enabled"), true}, {QStringLiteral("ready"), false}, {QStringLiteral("error"), error}};
  };
  const auto ref = clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString()));
  const auto* source = clipAt(ref);
  const auto* track = trackAt(ref.track_index);
  if (!source || !track || !sceneAttachmentGeometrySupported(*source, *track) ||
      !source->source_size.isValid() || source->source_size.isEmpty() || source->playback_rate <= 0 ||
      media_identity_signature(source->file_path) != source->source_identity_signature ||
      !timelineForegroundCutoutReady(source->foreground_cutout, source->source_identity_signature, source->trim_start_seconds, source->trim_end_seconds))
    return reject(QStringLiteral("The foreground occlusion matte is missing, stale, or incompatible with source framing. Regenerate it."));
  if (!std::isfinite(sequence_seconds) || sequence_seconds < source->start_seconds - kBoundaryEpsilon ||
      sequence_seconds >= source->start_seconds + clipTrimmedDuration(*source))
    return reject(QStringLiteral("The occlusion frame is outside the source shot."));
  QString error;
  const auto path = timelineForegroundMaskPath(source->foreground_cutout, source->source_identity_signature,
      clipLocalSecondsForSequenceTime(*source, qMax(sequence_seconds, source->start_seconds)), &error);
  if (path.isEmpty()) return reject(error);
  const auto aspect = static_cast<double>(source->source_size.width()) / qMax(1, source->source_size.height());
  const auto sequence_aspect = static_cast<double>(sequence_size_.width()) / sequence_size_.height();
  const auto width = qMin(1.0, aspect / sequence_aspect), height = qMin(1.0, sequence_aspect / aspect);
  return {{QStringLiteral("enabled"), true}, {QStringLiteral("ready"), true},
      {QStringLiteral("source"), QUrl::fromLocalFile(path).toString()},
      {QStringLiteral("rect"), QVariantMap{{QStringLiteral("x"), (1 - width) * 0.5},
          {QStringLiteral("y"), (1 - height) * 0.5}, {QStringLiteral("width"), width}, {QStringLiteral("height"), height}}}};
}

bool TimelineDocumentController::setSelectedSceneAttachmentOcclusion(bool enabled) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto* clip = clipAt(selectedClipRef());
  const auto* track = trackAt(selectedClipRef().track_index);
  if (!clip || !track || track->locked) return false;
  auto attachment = clip->motion_tracking.value(QStringLiteral("sceneAttachment")).toMap();
  if (attachment.isEmpty()) return false;
  if (attachment.value(QStringLiteral("occlusionEnabled")).toBool() == enabled) return true;
  if (enabled) {
    const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString())));
    if (!source || !timelineForegroundCutoutReady(source->foreground_cutout, source->source_identity_signature, source->trim_start_seconds, source->trim_end_seconds)) {
      tracking_status_ = QStringLiteral("Generate a foreground occlusion mask on the tracked source first.");
      emit trackingChanged(); setStatus(tracking_status_); return false;
    }
  }
  pushUndoSnapshot();
  attachment.insert(QStringLiteral("occlusionEnabled"), enabled);
  clip->motion_tracking.insert(QStringLiteral("sceneAttachment"), attachment);
  emitAllState(); setDirty(true); return true;
}

bool TimelineDocumentController::analyzeSelectedClipOcclusion(const QVariantMap& seed, bool temporal) {
  return startTrackingAnalysis(seed, true, temporal, false, false, true);
}

QVariantList TimelineDocumentController::bakeSceneAnchorSamples(const Clip& source, const QVariantMap& anchor, QString* error) const {
  const auto scene = source.motion_tracking.value(QStringLiteral("sceneCamera")).toMap().value(QStringLiteral("scene")).toMap();
  if (!timelineSceneIsValid(scene, error)) return {};
  if (source.playback_rate <= 0 || clipHasRetimeCurve(source)) {
    if (error) *error = QStringLiteral("Scene anchors require a forward, constant-speed source.");
    return {};
  }
  const auto aspect = static_cast<double>(scene.value(QStringLiteral("width")).toInt()) /
      qMax(1, scene.value(QStringLiteral("height")).toInt());
  const auto sequence_aspect = static_cast<double>(sequence_size_.width()) / sequence_size_.height();
  const auto ratio_x = qMin(1.0, aspect / sequence_aspect);
  const auto ratio_y = qMin(1.0, sequence_aspect / aspect);
  QVariantList samples;
  for (const auto& value : scene.value(QStringLiteral("frames")).toList()) {
    const auto seconds = value.toMap().value(QStringLiteral("sourceSeconds")).toDouble();
    auto projected = timelineSceneProjectAnchor(scene, anchor, seconds, error);
    if (projected.size() != 4) return {};
    QVariantList quad;
    for (const auto& corner : projected) {
      const auto point = corner.toList();
      if (point.size() != 2) { if (error) *error = QStringLiteral("Scene corner coordinates are invalid."); return {}; }
      const auto x = 0.5 + (point[0].toDouble() - 0.5) * ratio_x;
      const auto y = 0.5 + (point[1].toDouble() - 0.5) * ratio_y;
      if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 8 || qAbs(y) > 8) {
        if (error) *error = QStringLiteral("The plane leaves the supported projection range. Reduce its size or depth offset.");
        return {};
      }
      quad.append(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
    }
    samples.append(QVariantMap{{QStringLiteral("sequenceSeconds"), source.start_seconds +
        (seconds - source.trim_start_seconds) / source.playback_rate}, {QStringLiteral("quad"), quad}});
  }
  return samples;
}

QVariantList TimelineDocumentController::sceneAttachmentQuad(const Clip& clip, double sequence_seconds, QString* error) const {
  auto reject = [error](const QString& reason) -> QVariantList { if (error) *error = reason; return {}; };
  const auto attachment = clip.motion_tracking.value(QStringLiteral("sceneAttachment")).toMap();
  if (attachment.isEmpty()) return {};
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString())));
  const auto* track = trackAt(clipRefAt(flattenedIndexForClipId(clip.clip_id)).track_index);
  const auto surface = attachment.value(QStringLiteral("mode")).toString() == QStringLiteral("surface2d");
  if (!source || !track || !(surface ? surfaceTrackMetadataReady(*source) : sceneTrackMetadataReady(*source)) || !sceneAttachmentGeometrySupported(clip, *track))
    return reject(QStringLiteral("The scene source is missing or its framing changed. Reset transforms or reattach the plane."));
  const auto metadata = source->motion_tracking.value(surface ? QStringLiteral("surfaceTrack") : QStringLiteral("sceneCamera")).toMap();
  if (attachment.value(QStringLiteral("solveId")) != metadata.value(QStringLiteral("solveId")) ||
      attachment.value(QStringLiteral("sourceSignature")).toString() != source->source_identity_signature ||
      qAbs(attachment.value(QStringLiteral("sourceStart")).toDouble() - source->start_seconds) > kBoundaryEpsilon ||
      qAbs(attachment.value(QStringLiteral("sourceTrimStart")).toDouble() - source->trim_start_seconds) > kBoundaryEpsilon ||
      qAbs(attachment.value(QStringLiteral("sourceRate")).toDouble() - source->playback_rate) > kBoundaryEpsilon ||
      attachment.value(QStringLiteral("sequenceWidth")).toInt() != sequence_size_.width() ||
      attachment.value(QStringLiteral("sequenceHeight")).toInt() != sequence_size_.height())
    return reject(QStringLiteral("The tracked shot, solve, or sequence changed. Reattach the plane before preview or export."));
  if (attachment.value(QStringLiteral("occlusionEnabled")).toBool()) {
    const auto frame = sceneAttachmentOcclusionFrame(clip.clip_id, sequence_seconds);
    if (!frame.value(QStringLiteral("ready")).toBool()) return reject(frame.value(QStringLiteral("error")).toString());
  }
  const auto end = source->start_seconds + clipTrimmedDuration(*source);
  if (!std::isfinite(sequence_seconds) || sequence_seconds < source->start_seconds - kBoundaryEpsilon || sequence_seconds >= end + kBoundaryEpsilon)
    return reject(QStringLiteral("This layer is outside the solved shot. Trim it to the tracked source range."));
  const auto samples = attachment.value(QStringLiteral("samples")).toList();
  if (samples.size() < (surface ? 2 : 8) || samples.size() > 900) return reject(QStringLiteral("Scene attachment samples are incomplete."));
  int upper = 0;
  while (upper < samples.size() && samples[upper].toMap().value(QStringLiteral("sequenceSeconds")).toDouble() <= sequence_seconds) ++upper;
  const auto lower = qMax(0, upper - 1);
  upper = qMin(upper, static_cast<int>(samples.size()) - 1);
  const auto a = samples[lower].toMap();
  const auto b = samples[upper].toMap();
  const auto qa = a.value(QStringLiteral("quad")).toList();
  const auto qb = b.value(QStringLiteral("quad")).toList();
  if (qa.size() != 4 || qb.size() != 4) return reject(QStringLiteral("Scene attachment corners are invalid."));
  const auto ta = a.value(QStringLiteral("sequenceSeconds")).toDouble();
  const auto tb = b.value(QStringLiteral("sequenceSeconds")).toDouble();
  const auto weight = tb > ta ? qBound(0.0, (sequence_seconds - ta) / (tb - ta), 1.0) : 0.0;
  QVariantList quad;
  for (int corner = 0; corner < 4; ++corner) {
    const auto pa = qa[corner].toMap(); const auto pb = qb[corner].toMap();
    const auto x = pa.value(QStringLiteral("x")).toDouble() * (1 - weight) + pb.value(QStringLiteral("x")).toDouble() * weight;
    const auto y = pa.value(QStringLiteral("y")).toDouble() * (1 - weight) + pb.value(QStringLiteral("y")).toDouble() * weight;
    if (!std::isfinite(x) || !std::isfinite(y) || qAbs(x) > 8 || qAbs(y) > 8)
      return reject(QStringLiteral("Scene attachment contains invalid corners."));
    quad.append(QVariantMap{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}});
  }
  return quad;
}

bool TimelineDocumentController::attachSceneTrackToSelectedClip(const QString& source_clip_id, const QStringList& point_ids) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto reject = [this](const QString& reason) { tracking_status_ = reason; emit trackingChanged(); setStatus(reason); return false; };
  auto* target = clipAt(selectedClipRef());
  const auto* target_track = trackAt(selectedClipRef().track_index);
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(source_clip_id)));
  if (!target || !target_track || target_track->locked || target_track->kind != TrackKind::Video || target == source ||
      (target->media_type != QStringLiteral("title") && target->media_type != QStringLiteral("caption") && target->media_type != QStringLiteral("photo")))
    return reject(QStringLiteral("Select an unlocked title, caption, or image layer to attach to the solved scene."));
  if (!source || !sceneTrackMetadataReady(*source) || media_identity_signature(source->file_path) != source->source_identity_signature)
    return reject(QStringLiteral("The scene solve is missing or stale. Solve the source shot again."));
  if (!sceneAttachmentGeometrySupported(*target, *target_track) || qAbs(target->playback_rate - 1) > kBoundaryEpsilon)
    return reject(QStringLiteral("Use Fit framing and reset target/group transforms before attaching. Use Scene Placement controls afterward."));
  QString error;
  const auto metadata = source->motion_tracking.value(QStringLiteral("sceneCamera")).toMap();
  auto anchor = timelineSceneAnchorFromPoints(metadata.value(QStringLiteral("scene")).toMap(), point_ids, &error);
  if (anchor.isEmpty()) return reject(error);
  anchor.insert(QStringLiteral("height"), anchor.value(QStringLiteral("width")).toDouble() * sequence_size_.height() / sequence_size_.width());
  const auto samples = bakeSceneAnchorSamples(*source, anchor, &error);
  if (samples.isEmpty()) return reject(error);
  QVariantMap attachment{{QStringLiteral("sourceClipId"), source_clip_id}, {QStringLiteral("sourceSignature"), source->source_identity_signature},
      {QStringLiteral("solveId"), metadata.value(QStringLiteral("solveId"))}, {QStringLiteral("baseAnchor"), anchor}, {QStringLiteral("anchor"), anchor},
      {QStringLiteral("sourceStart"), source->start_seconds}, {QStringLiteral("sourceTrimStart"), source->trim_start_seconds},
      {QStringLiteral("sourceRate"), source->playback_rate}, {QStringLiteral("sequenceWidth"), sequence_size_.width()},
      {QStringLiteral("sequenceHeight"), sequence_size_.height()}, {QStringLiteral("samples"), samples},
      {QStringLiteral("widthScale"), 1.0}, {QStringLiteral("offsetX"), 0.0}, {QStringLiteral("offsetY"), 0.0},
      {QStringLiteral("offsetZ"), 0.0}, {QStringLiteral("rotationDegrees"), 0.0}};
  pushUndoSnapshot();
  target->motion_tracking.insert(QStringLiteral("sceneAttachment"), attachment);
  target->start_seconds = source->start_seconds;
  target->trim_end_seconds = target->trim_start_seconds + clipTrimmedDuration(*source);
  target->duration_seconds = qMax(target->duration_seconds, target->trim_end_seconds);
  emitAllState(); setDirty(true);
  tracking_status_ = QStringLiteral("Layer attached to a reconstructed 3D plane and aligned to the tracked shot. Undo restores its timing and placement.");
  emit trackingChanged(); setStatus(tracking_status_);
  return true;
}

bool TimelineDocumentController::setSelectedSceneAttachmentPlacement(const QVariantMap& placement) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto* target = clipAt(selectedClipRef());
  const auto* target_track = trackAt(selectedClipRef().track_index);
  if (!target || !target_track || target_track->locked) return false;
  auto attachment = target->motion_tracking.value(QStringLiteral("sceneAttachment")).toMap();
  if (attachment.isEmpty()) return false;
  QString error;
  if (sceneAttachmentQuad(*target, target->start_seconds, &error).isEmpty()) { setStatus(error); return false; }
  for (const auto& field : {QStringLiteral("widthScale"), QStringLiteral("offsetX"), QStringLiteral("offsetY"),
      QStringLiteral("offsetZ"), QStringLiteral("rotationDegrees")}) {
    const auto value = placement.value(field, attachment.value(field)).toDouble();
    const auto scale = field == QStringLiteral("widthScale");
    const auto limit = field == QStringLiteral("rotationDegrees") ? 180.0 : 20.0;
    if (!std::isfinite(value) || (scale && value < 0.05) || qAbs(value) > limit) return false;
    attachment.insert(field, value);
  }
  if (attachment.value(QStringLiteral("mode")).toString() == QStringLiteral("surface2d")) {
    if (qAbs(attachment.value(QStringLiteral("offsetZ")).toDouble()) > kBoundaryEpsilon) return false;
    const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString())));
    const auto samples = bakeSurfaceSamples(*source, attachment, &error);
    if (samples.isEmpty()) { setStatus(error); return false; }
    attachment.insert(QStringLiteral("samples"), samples);
    if (attachment == target->motion_tracking.value(QStringLiteral("sceneAttachment")).toMap()) return true;
    pushUndoSnapshot(); target->motion_tracking.insert(QStringLiteral("sceneAttachment"), attachment);
    emitAllState(); setDirty(true); return true;
  }
  const auto base = attachment.value(QStringLiteral("baseAnchor")).toMap();
  const auto vector = [](const QVariant& value) {
    const auto list = value.toList();
    return list.size() == 3 ? QVector3D(list[0].toFloat(), list[1].toFloat(), list[2].toFloat()) : QVector3D{};
  };
  const auto x = vector(base.value(QStringLiteral("axisX")));
  const auto y = vector(base.value(QStringLiteral("axisY")));
  const auto width = base.value(QStringLiteral("width")).toDouble();
  const auto center = vector(base.value(QStringLiteral("center"))) + static_cast<float>(width) *
      (x * attachment.value(QStringLiteral("offsetX")).toFloat() + y * attachment.value(QStringLiteral("offsetY")).toFloat() +
       QVector3D::crossProduct(x, y).normalized() * attachment.value(QStringLiteral("offsetZ")).toFloat());
  const auto angle = attachment.value(QStringLiteral("rotationDegrees")).toDouble() * std::numbers::pi / 180.0;
  const auto axis_x = x * static_cast<float>(std::cos(angle)) + y * static_cast<float>(std::sin(angle));
  const auto axis_y = y * static_cast<float>(std::cos(angle)) - x * static_cast<float>(std::sin(angle));
  const auto list = [](const QVector3D& value) -> QVariantList { return {value.x(), value.y(), value.z()}; };
  auto anchor = base;
  anchor.insert(QStringLiteral("center"), list(center)); anchor.insert(QStringLiteral("axisX"), list(axis_x));
  anchor.insert(QStringLiteral("axisY"), list(axis_y));
  anchor.insert(QStringLiteral("width"), width * attachment.value(QStringLiteral("widthScale")).toDouble());
  anchor.insert(QStringLiteral("height"), base.value(QStringLiteral("height")).toDouble() * attachment.value(QStringLiteral("widthScale")).toDouble());
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString())));
  const auto samples = bakeSceneAnchorSamples(*source, anchor, &error);
  if (samples.isEmpty()) { tracking_status_ = error; emit trackingChanged(); setStatus(error); return false; }
  attachment.insert(QStringLiteral("anchor"), anchor); attachment.insert(QStringLiteral("samples"), samples);
  if (attachment == target->motion_tracking.value(QStringLiteral("sceneAttachment")).toMap()) return true;
  pushUndoSnapshot(); target->motion_tracking.insert(QStringLiteral("sceneAttachment"), attachment);
  emitAllState(); setDirty(true); return true;
}

bool TimelineDocumentController::detachSelectedSceneAttachment() {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto* clip = clipAt(selectedClipRef());
  const auto* track = trackAt(selectedClipRef().track_index);
  if (!clip || !track || track->locked || !clip->motion_tracking.contains(QStringLiteral("sceneAttachment"))) return false;
  pushUndoSnapshot(); clip->motion_tracking.remove(QStringLiteral("sceneAttachment"));
  emitAllState(); setDirty(true); return true;
}

QString TimelineDocumentController::sceneAttachmentExportFilter(const Clip& clip, double sequence_start, double duration, const QSize& target_size, QString* error) const {
  if (!clip.motion_tracking.contains(QStringLiteral("sceneAttachment"))) return {};
  const auto end = sequence_start + duration - qMin(duration * 0.5, 0.5 / framesPerSecond());
  if (sceneAttachmentQuad(clip, sequence_start, error).isEmpty() || sceneAttachmentQuad(clip, end, error).isEmpty()) return {};
  const auto attachment = clip.motion_tracking.value(QStringLiteral("sceneAttachment")).toMap();
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(attachment.value(QStringLiteral("sourceClipId")).toString())));
  if (!source || media_identity_signature(source->file_path) != source->source_identity_signature) {
    if (error) *error = QStringLiteral("The tracked camera source changed. Solve and attach again before export.");
    return {};
  }
  auto samples = attachment.value(QStringLiteral("samples")).toList();
  // FFmpeg extends source-edge pixels outside a perspective quad. A one-pixel
  // transparent border, projected through the same homography, prevents an
  // opaque image from flooding the frame outside its actual scene plane.
  for (auto& value : samples) {
    auto sample = value.toMap();
    const auto quad = sample.value(QStringLiteral("quad")).toList();
    if (quad.size() != 4) { if (error) *error = QStringLiteral("Invalid scene quad."); return {}; }
    QPolygonF destination;
    for (int index : {0, 1, 3, 2}) {
      const auto point = quad[index].toMap();
      destination.append(QPointF(point.value(QStringLiteral("x")).toDouble(), point.value(QStringLiteral("y")).toDouble()));
    }
    QTransform transform;
    if (!QTransform::quadToQuad(QPolygonF{QPointF(0, 0), QPointF(1, 0), QPointF(1, 1), QPointF(0, 1)}, destination, transform)) {
      if (error) *error = QStringLiteral("The scene quad is degenerate."); return {};
    }
    const auto dx = 1.0 / target_size.width(); const auto dy = 1.0 / target_size.height();
    QVariantList expanded;
    for (const auto& corner : {QPointF(-dx, -dy), QPointF(1 + dx, -dy), QPointF(-dx, 1 + dy), QPointF(1 + dx, 1 + dy)}) {
      const auto point = transform.map(corner);
      expanded.append(QVariantMap{{QStringLiteral("x"), point.x()}, {QStringLiteral("y"), point.y()}});
    }
    sample.insert(QStringLiteral("quad"), expanded); value = sample;
  }
  QStringList options;
  for (int corner = 0; corner < 4; ++corner) {
    for (const auto& coordinate : {QStringLiteral("x"), QStringLiteral("y")}) {
      const auto first = samples.first().toMap().value(QStringLiteral("quad")).toList()[corner].toMap().value(coordinate).toDouble();
      auto expression = QString::number(first, 'g', 12);
      auto previous = first;
      auto previous_time = samples.first().toMap().value(QStringLiteral("sequenceSeconds")).toDouble();
      for (int index = 1; index < samples.size(); ++index) {
        const auto sample = samples[index].toMap();
        const auto time = sample.value(QStringLiteral("sequenceSeconds")).toDouble();
        const auto quad = sample.value(QStringLiteral("quad")).toList();
        if (quad.size() != 4 || !std::isfinite(time) || time <= previous_time) {
          if (error) *error = QStringLiteral("Scene attachment timing is invalid."); return {};
        }
        const auto value = quad[corner].toMap().value(coordinate).toDouble();
        if (!std::isfinite(value) || qAbs(value) > 8) { if (error) *error = QStringLiteral("Scene attachment corners are invalid."); return {}; }
        if (qAbs(value - previous) > 1.0e-10)
          expression += QStringLiteral("+(%1)*clip((on/%2+%3-%4)/%5,0,1)")
              .arg(QString::number(value - previous, 'g', 12), QString::number(framesPerSecond(), 'g', 12),
                   QString::number(sequence_start, 'g', 12), QString::number(previous_time, 'g', 12),
                   QString::number(time - previous_time, 'g', 12));
        previous = value; previous_time = time;
      }
      options.append(QStringLiteral("%1%2='(%3-2)*(%4)+1'").arg(coordinate).arg(corner)
          .arg(coordinate == QStringLiteral("x") ? QStringLiteral("W") : QStringLiteral("H"), expression));
    }
  }
  return QStringLiteral("fps=%1,format=rgba,pad=iw+2:ih+2:1:1:color=black@0,perspective=%2:sense=destination:eval=frame:interpolation=cubic,crop=iw-2:ih-2:1:1,")
      .arg(QString::number(framesPerSecond(), 'g', 12), options.join(QLatin1Char(':')));
}
