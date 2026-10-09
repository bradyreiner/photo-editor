// Included by timeline_document_controller.cpp: shares the document's snapshot,
// access, source identity, and rendering contracts without another QObject.
bool TimelineDocumentController::trackingBusy() const { return tracking_watcher_ != nullptr; }
double TimelineDocumentController::trackingProgress() const { return tracking_progress_; }
QString TimelineDocumentController::trackingStatus() const { return tracking_status_; }

QVariantList TimelineDocumentController::motionTrackSources() const {
  QVariantList sources;
  for (const auto& track : tracks_) {
    if (track.kind != TrackKind::Video) continue;
    for (const auto& clip : track.clips) {
      if (clip.motion_tracking.value(QStringLiteral("samples")).toList().isEmpty() ||
          clip.motion_tracking.value(QStringLiteral("sourceSignature")).toString() != clip.source_identity_signature) continue;
      sources.append(QVariantMap{{QStringLiteral("clipId"), clip.clip_id},
          {QStringLiteral("displayLabel"), clip.display_label},
          {QStringLiteral("motionTracking"), clip.motion_tracking}});
    }
  }
  return sources;
}

void TimelineDocumentController::cancelTrackingAnalysis() {
  if (!tracking_cancel_token_) return;
  tracking_cancel_token_->store(true);
  tracking_status_ = QStringLiteral("Cancelling tracking analysis…");
  emit trackingChanged();
}

void TimelineDocumentController::resetTrackingAnalysis() {
  if (tracking_cancel_token_) tracking_cancel_token_->store(true);
  if (tracking_watcher_) {
    auto* abandoned = tracking_watcher_;
    tracking_watcher_ = nullptr;
    abandoned->disconnect(this);
    abandoned->setParent(nullptr);
    if (abandoned->isFinished()) {
      const auto directory = abandoned->result().value(QStringLiteral("directory")).toString();
      if (!directory.isEmpty()) QDir(directory).removeRecursively();
      abandoned->deleteLater();
    } else connect(abandoned, &QFutureWatcher<QVariantMap>::finished, abandoned,
        [abandoned]() {
          const auto directory = abandoned->result().value(QStringLiteral("directory")).toString();
          if (!directory.isEmpty()) QDir(directory).removeRecursively();
          abandoned->deleteLater();
        });
  }
  tracking_cancel_token_.reset();
  tracking_progress_ = 0.0;
  tracking_status_ = QStringLiteral("Track a region or generate a foreground cutout locally.");
  emit trackingChanged();
}

bool TimelineDocumentController::analyzeSelectedClipMotion(const QVariantMap& seed) {
  return startTrackingAnalysis(seed, false);
}
bool TimelineDocumentController::analyzeSelectedClipCutout() {
  return startTrackingAnalysis({}, true);
}

bool TimelineDocumentController::rotoscopingAvailable() const { return timelineSam2Available(); }
bool TimelineDocumentController::analyzeSelectedClipRotoscope(const QVariantMap& seed) {
  return startTrackingAnalysis(seed, true, true);
}

QVariantMap TimelineDocumentController::rotoscopeCorrectionFrame() const {
  const auto* clip = clipAt(selectedClipRef());
  if (!clip || clip != clipAt(programClipRef()) || clip->media_type != QStringLiteral("video"))
    return {{QStringLiteral("success"), false}, {QStringLiteral("message"), QStringLiteral("Select the source video at the paused playhead.")}};
  const auto data = clip->foreground_cutout;
  if (data.value(QStringLiteral("backend")).toString() != QStringLiteral("sam2-temporal") ||
      !timelineForegroundCutoutReady(data, clip->source_identity_signature, clip->trim_start_seconds, clip->trim_end_seconds))
    return {{QStringLiteral("success"), false}, {QStringLiteral("message"), QStringLiteral("Generate a SAM2 temporal mask first.")}};
  const auto fps = data.value(QStringLiteral("fps")).toDouble();
  const auto index = qFloor((currentClipPlayheadSeconds() - data.value(QStringLiteral("trimStart")).toDouble()) * fps + 0.000001);
  const auto frames = data.value(QStringLiteral("previewFrames")).toList();
  if (index < 0 || index >= frames.size()) return {{QStringLiteral("success"), false}, {QStringLiteral("message"), QStringLiteral("The correction frame is unavailable. Regenerate the mask.")}};
  const auto frame = frames[index].toMap();
  const auto path = frame.value(QStringLiteral("path")).toString();
  if (!QFileInfo(path).isReadable()) return {{QStringLiteral("success"), false}, {QStringLiteral("message"), QStringLiteral("The correction image is missing. Regenerate the mask.")}};
  QVariantList points;
  for (const auto& value : data.value(QStringLiteral("seed")).toMap().value(QStringLiteral("correctionPrompts")).toList()) {
    const auto prompt = value.toMap();
    if (prompt.value(QStringLiteral("frameIndex")).toInt() == index) points = prompt.value(QStringLiteral("points")).toList();
  }
  return {{QStringLiteral("success"), true}, {QStringLiteral("path"), QUrl::fromLocalFile(path).toString()},
      {QStringLiteral("sourceSeconds"), frame.value(QStringLiteral("sourceSeconds"))}, {QStringLiteral("frameIndex"), index},
      {QStringLiteral("points"), points}};
}

bool TimelineDocumentController::correctSelectedRotoscope(const QVariantList& points) {
  const auto frame = rotoscopeCorrectionFrame();
  const auto* clip = clipAt(selectedClipRef());
  if (!clip || !frame.value(QStringLiteral("success")).toBool() || points.isEmpty() || points.size() > 256) return false;
  for (const auto& value : points) {
    const auto point = value.toMap();
    bool x_ok = false, y_ok = false, label_ok = false;
    const auto x = point.value(QStringLiteral("x")).toDouble(&x_ok), y = point.value(QStringLiteral("y")).toDouble(&y_ok);
    const auto label = point.value(QStringLiteral("label")).toInt(&label_ok);
    if (!x_ok || !y_ok || !label_ok || !std::isfinite(x) || !std::isfinite(y) || x < 0 || x > 1 || y < 0 || y > 1 || (label != 0 && label != 1)) return false;
  }
  auto seed = clip->foreground_cutout.value(QStringLiteral("seed")).toMap();
  QVariantList prompts;
  for (const auto& value : seed.value(QStringLiteral("correctionPrompts")).toList())
    if (value.toMap().value(QStringLiteral("frameIndex")).toInt() != frame.value(QStringLiteral("frameIndex")).toInt()) prompts.append(value);
  if (prompts.size() >= 64) return false;
  prompts.append(QVariantMap{{QStringLiteral("frameIndex"), frame.value(QStringLiteral("frameIndex"))}, {QStringLiteral("points"), points}});
  seed.insert(QStringLiteral("correctionPrompts"), prompts);
  return startTrackingAnalysis(seed, true, true, false, false, !clip->foreground_cutout.value(QStringLiteral("enabled")).toBool());
}

bool TimelineDocumentController::startTrackingAnalysis(const QVariantMap& seed, bool foreground, bool temporal, bool scene, bool surface, bool occlusion) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto reject = [this](const QString& message) {
    tracking_status_ = message;
    emit trackingChanged();
    setStatus(message);
    return false;
  };
  if (trackingBusy()) return reject(QStringLiteral("Wait for the current analysis or cancel it first."));
  const auto ref = selectedClipRef();
  const auto* clip = clipAt(ref);
  const auto* track = trackAt(ref.track_index);
  if (!clip || !track || track->locked || clip->media_type != QStringLiteral("video") ||
      !QFileInfo(clip->file_path).isReadable())
    return reject(QStringLiteral("Select an unlocked, online video clip to analyze."));
  QString surface_reason;
  if (surface && !timelineSurfaceAvailable(&surface_reason)) return reject(surface_reason);
  QString sam2_reason;
  if (temporal && !timelineSam2Available(&sam2_reason)) return reject(sam2_reason);
  if (scene && !sceneTrackingAvailable()) return reject(QStringLiteral("Install COLMAP or set PHOTO_EDITOR_COLMAP_BINARY before solving scene cameras."));
  if ((scene || surface) && mediaAnalysisBusy()) return reject(QStringLiteral("Wait for source metadata and thumbnails to finish loading before solving the scene."));
  if (!scene && !surface && !temporal && !timelineMotionAnalysisAvailable())
    return reject(QStringLiteral("Native timeline tracking requires macOS with Apple Vision; foreground cutouts require macOS 14 or later."));
  const QRectF box(seed.value(QStringLiteral("x")).toDouble(), seed.value(QStringLiteral("y")).toDouble(),
      seed.value(QStringLiteral("width")).toDouble(), seed.value(QStringLiteral("height")).toDouble());
  if (!scene && (!foreground || temporal) && !timelineNormalizedBoxIsValid(box))
    return reject(QStringLiteral("Choose a valid subject region on the first source frame."));
  if ((foreground || scene || surface) && (clip->stabilization_enabled || clip->rolling_shutter_enabled))
    return reject(QStringLiteral("Disable stabilization and rolling-shutter correction before source-space cutout analysis."));
  const auto source_duration = clip->trim_end_seconds - clip->trim_start_seconds;
  const auto fps = qBound(1.0, framesPerSecond(), (scene || surface) ? 30.0 : 60.0);
  if ((scene || surface) && (!clip->source_size.isValid() || clip->source_size.isEmpty()))
    return reject(QStringLiteral("Wait for source metadata to finish loading before solving the scene."));
  if ((scene || surface) && !sceneAttachmentGeometrySupported(*clip, *track))
    return reject(QStringLiteral("Solve an untransformed Fit source without group transforms, output framing overrides, or retiming."));
  if ((scene || surface) && (source_duration * fps < (surface ? 2 : 8) || source_duration * fps > 900 || clipHasRetimeCurve(*clip)))
    return reject(surface ? QStringLiteral("Surface tracking needs 2–900 source frames and a constant-speed shot. Trim or split longer shots.")
        : QStringLiteral("Scene solving needs 8–900 source frames and a constant-speed shot. Trim or split longer shots."));
  if (!std::isfinite(source_duration) || source_duration * fps < 2 || source_duration * fps > kTimelineMotionMaximumFrames)
    return reject(QStringLiteral("Trim the source range to 2–900 frames before analysis (%1 fps). Long shots can be split.").arg(fps));
  if (temporal && source_duration * fps > 180)
    return reject(QStringLiteral("Trim SAM2 analysis to at most 180 source frames to bound temporal-model memory. Split longer shots."));
  const auto count = static_cast<int>(qCeil(source_duration * fps - 1.0e-7));
  if (count < 2 || count > kTimelineMotionMaximumFrames)
    return reject(QStringLiteral("Trim the source range to 2–900 frames before analysis (%1 fps). Long shots can be split.").arg(fps));
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return reject(QStringLiteral("FFmpeg is required to decode timeline analysis frames."));
  if ((!foreground || temporal) && (clip->frame_mode != QStringLiteral("fit") || qAbs(clip->frame_scale - 1) > kBoundaryEpsilon ||
      qAbs(clip->frame_offset_x) > kBoundaryEpsilon || qAbs(clip->frame_offset_y) > kBoundaryEpsilon ||
      qAbs(clip->frame_rotation_degrees) > kBoundaryEpsilon || qAbs(clip->frame_perspective_x) > kBoundaryEpsilon ||
      qAbs(clip->frame_perspective_y) > kBoundaryEpsilon ||
      qAbs(track->group_transform_scale - 1) > kBoundaryEpsilon ||
      qAbs(track->group_transform_offset_x) > kBoundaryEpsilon || qAbs(track->group_transform_offset_y) > kBoundaryEpsilon ||
      qAbs(track->group_transform_rotation_degrees) > kBoundaryEpsilon))
    return reject(QStringLiteral("Track an untransformed Fit source, then attach its motion to a graphic or another clip."));

  if (!foreground || temporal) {
    for (const auto& parameter : {QStringLiteral("frameScale"), QStringLiteral("frameOffsetX"), QStringLiteral("frameOffsetY"),
        QStringLiteral("frameRotationDegrees"), QStringLiteral("framePerspectiveX"), QStringLiteral("framePerspectiveY")})
      if (clipHasKeyframesFor(*clip, parameter)) return reject(QStringLiteral("Use a source without transform animation for tracking/box-seeded rotoscoping."));
  }
  const auto snapshot = *clip;
  const auto revision = document_edit_revision_;
  const auto timeline_id = timeline_id_;
  const auto path = clip->file_path;
  const auto signature = media_identity_signature(path);
  if (signature != clip->source_identity_signature) return reject(QStringLiteral("The source identity changed. Relink or reopen this clip before analysis."));
  const auto root = QDir(timelineDocumentsRootPath()).filePath(QStringLiteral("tracking"));
  if ((foreground || scene || surface) && !QDir().mkpath(root)) return reject(QStringLiteral("Unable to create the local tracking cache."));
  tracking_progress_ = 0;
  tracking_status_ = surface ? QStringLiteral("Tracking multiple features on the selected surface…") : scene ? QStringLiteral("Decoding frames for a multi-point 3D camera solve…")
      : foreground ? QStringLiteral("Generating per-frame foreground masks locally…")
                                : QStringLiteral("Tracking the first-frame region locally…");
  tracking_cancel_token_ = std::make_shared<std::atomic_bool>(false);
  const auto token = tracking_cancel_token_;
  tracking_watcher_ = new QFutureWatcher<QVariantMap>(this);
  auto* watcher = tracking_watcher_;
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
      [this, watcher, token, snapshot, revision, timeline_id, signature, foreground, temporal, scene, surface, occlusion]() {
    if (watcher != tracking_watcher_) return;
    const auto result = watcher->result();
    tracking_watcher_ = nullptr;
    watcher->deleteLater();
    auto* destination = clipAt(clipRefAt(flattenedIndexForClipId(snapshot.clip_id)));
    const auto directory = result.value(QStringLiteral("directory")).toString();
    auto comparable_destination = destination ? *destination : Clip{};
    // A background full-file checksum enriches the same ingested source. It
    // does not change the frames used by this solve. Keep every edit-sensitive
    // field and the fast on-disk identity in the stale-result comparison.
    comparable_destination.source_content_sha256 = snapshot.source_content_sha256;
    comparable_destination.source_integrity_status = snapshot.source_integrity_status;
    const auto stale = timeline_id_ != timeline_id || document_edit_revision_ != revision ||
        !destination || !(comparable_destination == snapshot) ||
        destination->source_integrity_status == QStringLiteral("mismatch") ||
        media_identity_signature(snapshot.file_path) != signature;
    if (token->load() || stale || !result.value(QStringLiteral("success")).toBool()) {
      if (!token->load() && !stale && surface && !directory.isEmpty()) {
        pushUndoSnapshot();
        destination->motion_tracking.insert(QStringLiteral("surfaceTrackDraft"), result.value(QStringLiteral("metadata")).toMap());
        emitAllState();
        setDirty(true);
      } else if (!directory.isEmpty()) QDir(directory).removeRecursively();
      tracking_status_ = token->load() ? QStringLiteral("Analysis cancelled. The clip was not changed.")
          : stale ? QStringLiteral("The document or source changed during analysis. Analyze again.")
                  : result.value(QStringLiteral("message")).toString();
    } else {
      pushUndoSnapshot();
      if (foreground) destination->foreground_cutout = result.value(QStringLiteral("metadata")).toMap();
      else if (surface) {
        destination->motion_tracking.insert(QStringLiteral("surfaceTrack"), result.value(QStringLiteral("metadata")).toMap());
        destination->motion_tracking.remove(QStringLiteral("surfaceTrackDraft"));
      }
      else if (scene) destination->motion_tracking.insert(QStringLiteral("sceneCamera"), result.value(QStringLiteral("metadata")).toMap());
      else {
        const auto surface_track = destination->motion_tracking.value(QStringLiteral("surfaceTrack"));
        const auto scene_camera = destination->motion_tracking.value(QStringLiteral("sceneCamera"));
        destination->motion_tracking = result.value(QStringLiteral("metadata")).toMap();
        if (surface_track.isValid()) destination->motion_tracking.insert(QStringLiteral("surfaceTrack"), surface_track);
        if (scene_camera.isValid()) destination->motion_tracking.insert(QStringLiteral("sceneCamera"), scene_camera);
      }
      tracking_progress_ = 1;
      tracking_status_ = surface ? QStringLiteral("Surface tracked. Select a title or image and attach it to this surface.") : scene ? QStringLiteral("3D scene solved. Select three static points on one surface, then attach a title or image.")
          : foreground ? (occlusion ? QStringLiteral("Occlusion mask ready. Attach content and enable foreground occlusion; the source stays complete.") : temporal ? QStringLiteral("SAM2 temporal cutout ready. Inspect occlusion and edges before delivery.") : QStringLiteral("Foreground cutout ready. Masks are independent per frame; inspect edges and flicker."))
          : QStringLiteral("Motion track saved. Select a graphic or clip and attach this track.");
      emitAllState();
      setDirty(true);
    }
    emit trackingChanged();
    setStatus(tracking_status_);
  });
  QPointer<TimelineDocumentController> guard(this);
  watcher->setFuture(QtConcurrent::run([ffmpeg, path, signature, snapshot, fps, count, root, foreground, temporal, scene, surface, occlusion, seed, box, token, guard]() -> QVariantMap {
    QTemporaryDir directory((foreground || scene || surface) ? QDir(root).filePath(QStringLiteral("analysis-XXXXXX")) : QString{});
    if ((foreground || scene || surface) && !directory.isValid()) return {{QStringLiteral("message"), QStringLiteral("Unable to stage analysis frames.")}};
    const auto source_size = snapshot.source_size.isValid() ? snapshot.source_size : QSize(640, 360);
    const auto factor = qMin(1.0, ((scene || surface) ? 960.0 : 640.0) / qMax(source_size.width(), source_size.height()));
    const auto width = qMax(2, qRound(source_size.width() * factor) / 2 * 2);
    const auto height = qMax(2, qRound(source_size.height() * factor) / 2 * 2);
    QProcess decode;
    decode.start(ffmpeg, {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-nostdin"),
        QStringLiteral("-ss"), QString::number(snapshot.trim_start_seconds, 'f', 9),
        QStringLiteral("-i"), path, QStringLiteral("-an"), QStringLiteral("-sn"),
        QStringLiteral("-vf"), QStringLiteral("scale=%1:%2,fps=%3").arg(width).arg(height).arg(fps, 0, 'f', 9),
        QStringLiteral("-frames:v"), QString::number(count), QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    if (!decode.waitForStarted(3000)) return {{QStringLiteral("message"), QStringLiteral("Unable to start the analysis decoder.")}};
    const auto bytes_per_frame = static_cast<qsizetype>(width) * height * 4;
    const TimelineMotionFrameProvider provider = [&decode, token, bytes_per_frame, width, height, fps, snapshot](int index, TimelineMotionFrame* frame, QString* error) {
      QByteArray bytes;
      bytes.reserve(bytes_per_frame);
      QElapsedTimer deadline;
      deadline.start();
      while (bytes.size() < bytes_per_frame && !token->load()) {
        const auto chunk = decode.read(bytes_per_frame - bytes.size());
        bytes.append(chunk);
        if (bytes.size() == bytes_per_frame) break;
        if (decode.state() == QProcess::NotRunning || deadline.elapsed() > 15000) {
          *error = QStringLiteral("The decoder did not supply every requested source frame: %1").arg(QString::fromUtf8(decode.readAllStandardError()).left(400));
          return false;
        }
        decode.waitForReadyRead(100);
      }
      if (token->load()) return false;
      frame->source_time_us = qRound64((snapshot.trim_start_seconds + static_cast<double>(index) / fps) * 1000000.0);
      frame->image = QImage(reinterpret_cast<const uchar*>(bytes.constData()), width, height, width * 4, QImage::Format_RGBA8888).copy();
      return !frame->image.isNull();
    };
    const TimelineAnalysisProgress progress = [guard, token](int completed, int total) {
      if (!guard) return;
      QMetaObject::invokeMethod(guard, [guard, token, completed, total]() {
        if (!guard || guard->tracking_cancel_token_ != token || token->load()) return;
        guard->tracking_progress_ = static_cast<double>(completed) / qMax(1, total);
        emit guard->trackingChanged();
      }, Qt::QueuedConnection);
    };
    QVariantMap metadata{{QStringLiteral("sourceSignature"), signature},
        {QStringLiteral("trimStart"), snapshot.trim_start_seconds}, {QStringLiteral("trimEnd"), snapshot.trim_end_seconds},
        {QStringLiteral("fps"), fps}, {QStringLiteral("frameCount"), count}};
    bool success = false;
    QString message;
    if (surface) {
      const auto frames_dir = QDir(directory.path()).filePath(QStringLiteral("preview"));
      success = QDir().mkpath(frames_dir);
      QVariantList source_times, preview_frames;
      for (int index = 0; index < count && success && !token->load(); ++index) {
        TimelineMotionFrame frame;
        const auto preview_path = QDir(frames_dir).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
        success = provider(index, &frame, &message) && frame.image.save(preview_path, "PNG");
        const auto seconds = static_cast<double>(frame.source_time_us) / 1000000.0;
        source_times.append(seconds);
        preview_frames.append(QVariantMap{{QStringLiteral("path"), preview_path}, {QStringLiteral("sourceSeconds"), seconds}});
        progress(index + 1, count * 2);
      }
      decode.kill(); decode.waitForFinished(3000);
      if (success && !token->load()) {
        const auto analysis = trackTimelineSurface(frames_dir, source_times, seed, token.get(),
            [progress, count](int completed, int) { progress(count + completed, count * 2); });
        success = analysis.value(QStringLiteral("success")).toBool();
        message = analysis.value(QStringLiteral("message")).toString();
        if (success) {
          for (auto it = analysis.cbegin(); it != analysis.cend(); ++it) metadata.insert(it.key(), it.value());
          metadata.insert(QStringLiteral("seed"), seed);
          metadata.insert(QStringLiteral("schemaVersion"), 1);
          metadata.insert(QStringLiteral("solveId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
          metadata.insert(QStringLiteral("previewFrames"), preview_frames);
          metadata.insert(QStringLiteral("directory"), directory.path());
          directory.setAutoRemove(false);
        } else if (!token->load()) {
          metadata.insert(QStringLiteral("seed"), seed);
          metadata.insert(QStringLiteral("previewFrames"), preview_frames);
          metadata.insert(QStringLiteral("width"), width);
          metadata.insert(QStringLiteral("height"), height);
          metadata.insert(QStringLiteral("error"), message);
          metadata.insert(QStringLiteral("directory"), directory.path());
          directory.setAutoRemove(false);
        }
      }
    } else if (scene) {
      const auto frames_dir = QDir(directory.path()).filePath(QStringLiteral("frames"));
      const auto preview_dir = QDir(directory.path()).filePath(QStringLiteral("preview"));
      success = QDir().mkpath(frames_dir) && QDir().mkpath(preview_dir);
      QVector<qint64> source_times;
      QVariantList preview_frames;
      source_times.reserve(count);
      for (int index = 0; index < count && success && !token->load(); ++index) {
        TimelineMotionFrame frame;
        success = provider(index, &frame, &message) && frame.image.save(
            QDir(frames_dir).filePath(QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'))), "JPEG", 98);
        const auto preview_path = QDir(preview_dir).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
        success = success && frame.image.save(preview_path, "PNG");
        preview_frames.append(QVariantMap{{QStringLiteral("path"), preview_path},
            {QStringLiteral("sourceSeconds"), static_cast<double>(frame.source_time_us) / 1000000.0}});
        source_times.append(frame.source_time_us);
        progress(index + 1, count * 5);
      }
      decode.kill(); decode.waitForFinished(3000);
      if (success && !token->load()) {
        TimelineSceneSolveOptions options;
        options.cancelled = token.get();
        options.progress = [progress, count, guard, token](const QString& stage, int completed, int total) {
          progress(count + count * 4 * completed / qMax(1, total), count * 5);
          if (guard) QMetaObject::invokeMethod(guard, [guard, token, stage]() {
            if (!guard || guard->tracking_cancel_token_ != token || token->load()) return;
            guard->tracking_status_ = stage;
            emit guard->trackingChanged();
          }, Qt::QueuedConnection);
        };
        const auto analysis = solveTimelineScene({frames_dir, source_times}, options);
        success = analysis.status == TimelineSceneStatus::Success;
        message = analysis.message;
        if (success) {
          const auto solved = timelineSceneResultToMap(analysis);
          metadata.insert(QStringLiteral("scene"), solved);
          metadata.insert(QStringLiteral("solveId"), QUuid::createUuid().toString(QUuid::WithoutBraces));
          metadata.insert(QStringLiteral("backend"), QStringLiteral("colmap-camera-solve"));
          QVariantList points;
          for (const auto& value : timelineSceneVisiblePoints(solved, static_cast<double>(source_times.first()) / 1000000.0)) {
            const auto point = value.toMap();
            const auto position = point.value(QStringLiteral("source")).toList();
            if (position.size() == 2) points.append(QVariantMap{{QStringLiteral("id"), point.value(QStringLiteral("id"))},
                {QStringLiteral("x"), position[0]}, {QStringLiteral("y"), position[1]}, {QStringLiteral("error"), point.value(QStringLiteral("error"))}});
          }
          metadata.insert(QStringLiteral("firstFramePoints"), points);
          metadata.insert(QStringLiteral("previewFrames"), preview_frames);
          metadata.insert(QStringLiteral("directory"), directory.path());
          directory.setAutoRemove(false);
        }
      }
    } else if (temporal) {
      QTemporaryDir decoded_frames(QDir(root).filePath(QStringLiteral("frames-XXXXXX")));
      const auto frames_dir = decoded_frames.path();
      const auto preview_dir = QDir(directory.path()).filePath(QStringLiteral("preview"));
      const auto masks_dir = QDir(directory.path()).filePath(QStringLiteral("masks"));
      QVariantList preview_frames;
      success = decoded_frames.isValid() && QDir().mkpath(preview_dir);
      for (int index = 0; index < count && success && !token->load(); ++index) {
        TimelineMotionFrame frame;
        success = provider(index, &frame, &message) && frame.image.save(
            QDir(frames_dir).filePath(QStringLiteral("%1.jpg").arg(index, 6, 10, QLatin1Char('0'))), "JPEG", 95);
        const auto preview_path = QDir(preview_dir).filePath(QStringLiteral("%1.png").arg(index, 6, 10, QLatin1Char('0')));
        success = success && frame.image.save(preview_path, "PNG");
        preview_frames.append(QVariantMap{{QStringLiteral("path"), preview_path},
            {QStringLiteral("sourceSeconds"), static_cast<double>(frame.source_time_us) / 1000000.0}});
        progress(index + 1, count * 2);
      }
      decode.kill(); decode.waitForFinished(3000);
      if (success && !token->load()) {
        TimelineSam2Options options;
        options.cancelled = token.get();
        options.progress = [progress, count](int completed, int) { progress(count + completed, count * 2); };
        const auto analysis = runTimelineSam2Tracking({frames_dir, masks_dir, count, box,
            seed.value(QStringLiteral("correctionPrompts")).toList()}, options);
        success = analysis.status == TimelineSam2Status::Success && analysis.mask_paths.size() == count;
        message = analysis.message;
      }
      QDir(frames_dir).removeRecursively();
      if (success && !token->load()) {
        metadata.insert(QStringLiteral("directory"), masks_dir);
        metadata.insert(QStringLiteral("enabled"), !occlusion);
        metadata.insert(QStringLiteral("backend"), QStringLiteral("sam2-temporal"));
        metadata.insert(QStringLiteral("seed"), seed);
        metadata.insert(QStringLiteral("previewFrames"), preview_frames);
        directory.setAutoRemove(false);
      }
    } else if (foreground) {
      const auto analysis = analyzeTimelineForeground(count, provider, token.get(), progress);
      message = analysis.message;
      success = analysis.status == TimelineAnalysisStatus::Success && analysis.masks.size() == count;
      if (success) {
        for (int index = 0; index < analysis.masks.size() && success && !token->load(); ++index) {
          const auto& mask = analysis.masks[index];
          QImage image(mask.width, mask.height, QImage::Format_ARGB32);
          for (int y = 0; y < mask.height; ++y) {
            auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
            for (int x = 0; x < mask.width; ++x)
              row[x] = qRgba(255, 255, 255, static_cast<unsigned char>(mask.alpha.at(y * mask.width + x)));
          }
          success = image.save(QDir(directory.path()).filePath(QStringLiteral("mask_%1.png").arg(index, 6, 10, QLatin1Char('0'))));
        }
        if (!success) message = QStringLiteral("Unable to save every foreground mask.");
      }
      if (success && !token->load()) {
        metadata.insert(QStringLiteral("directory"), directory.path());
        metadata.insert(QStringLiteral("enabled"), !occlusion);
        metadata.insert(QStringLiteral("backend"), QStringLiteral("apple-vision-independent-foreground"));
        directory.setAutoRemove(false);
      }
    } else {
      const auto analysis = trackTimelineObject(count, provider, box, token.get(), progress);
      message = analysis.message;
      success = analysis.status == TimelineAnalysisStatus::Success && analysis.samples.size() == count;
      QVariantList samples;
      for (const auto& sample : analysis.samples) samples.append(QVariantMap{
          {QStringLiteral("sourceSeconds"), static_cast<double>(sample.source_time_us) / 1000000.0},
          {QStringLiteral("x"), sample.normalized_bounds.x()}, {QStringLiteral("y"), sample.normalized_bounds.y()},
          {QStringLiteral("width"), sample.normalized_bounds.width()}, {QStringLiteral("height"), sample.normalized_bounds.height()},
          {QStringLiteral("confidence"), sample.confidence}});
      metadata.insert(QStringLiteral("samples"), samples);
      metadata.insert(QStringLiteral("seed"), QVariantMap{{QStringLiteral("x"), box.x()}, {QStringLiteral("y"), box.y()}, {QStringLiteral("width"), box.width()}, {QStringLiteral("height"), box.height()}});
      metadata.insert(QStringLiteral("backend"), QStringLiteral("apple-vision-object-tracking"));
    }
    decode.kill();
    decode.waitForFinished(3000);
    if (token->load()) success = false;
    return {{QStringLiteral("success"), success}, {QStringLiteral("message"), message},
        {QStringLiteral("metadata"), metadata},
        // Matte metadata points at masks/, but abandoned results own the
        // complete analysis root, including retained source previews.
        {QStringLiteral("directory"), metadata.value(QStringLiteral("directory")).toString().isEmpty()
            ? QString{} : directory.path()}};
  }));
  emit trackingChanged();
  return true;
}

bool TimelineDocumentController::setSelectedClipForegroundCutout(bool enabled) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto* clip = clipAt(selectedClipRef());
  const auto* track = trackAt(selectedClipRef().track_index);
  if (!clip || !track || track->locked) return false;
  if (enabled && !timelineForegroundCutoutReady(clip->foreground_cutout, clip->source_identity_signature,
      clip->trim_start_seconds, clip->trim_end_seconds)) {
    setStatus(QStringLiteral("Generate a complete foreground cutout for the current source range first."));
    return false;
  }
  if (clip->foreground_cutout.value(QStringLiteral("enabled")).toBool() == enabled) return true;
  pushUndoSnapshot();
  clip->foreground_cutout.insert(QStringLiteral("enabled"), enabled);
  emitAllState();
  setDirty(true);
  return true;
}

bool TimelineDocumentController::attachMotionTrackToSelectedClip(const QString& source_clip_id) {
  const auto access = customer_edit_access_.enter();
  if (!access) return false;
  auto* target = clipAt(selectedClipRef());
  const auto* target_track = trackAt(selectedClipRef().track_index);
  const auto* source = clipAt(clipRefAt(flattenedIndexForClipId(source_clip_id)));
  auto reject = [this](const QString& message) { tracking_status_ = message; emit trackingChanged(); setStatus(message); return false; };
  if (!target || !source || target == source || !target_track || target_track->locked ||
      target_track->kind != TrackKind::Video || target->media_type == QStringLiteral("adjustment"))
    return reject(QStringLiteral("Select an unlocked graphic or visual clip, then choose a different tracked source."));
  const auto* source_track = trackAt(clipRefAt(flattenedIndexForClipId(source_clip_id)).track_index);
  const auto has_transform_animation = [this](const Clip& candidate) {
    for (const auto& parameter : {QStringLiteral("frameScale"), QStringLiteral("frameOffsetX"), QStringLiteral("frameOffsetY"),
        QStringLiteral("frameRotationDegrees"), QStringLiteral("framePerspectiveX"), QStringLiteral("framePerspectiveY")})
      if (clipHasKeyframesFor(candidate, parameter)) return true;
    return false;
  };
  if (source->frame_mode != QStringLiteral("fit") || qAbs(source->frame_scale - 1) > kBoundaryEpsilon ||
      qAbs(source->frame_offset_x) > kBoundaryEpsilon || qAbs(source->frame_offset_y) > kBoundaryEpsilon ||
      qAbs(source->frame_rotation_degrees) > kBoundaryEpsilon || qAbs(source->frame_perspective_x) > kBoundaryEpsilon ||
      qAbs(source->frame_perspective_y) > kBoundaryEpsilon || has_transform_animation(*source) ||
      !source->output_variant_overrides.isEmpty() || !source_track ||
      qAbs(source_track->group_transform_scale - 1) > kBoundaryEpsilon ||
      qAbs(source_track->group_transform_offset_x) > kBoundaryEpsilon || qAbs(source_track->group_transform_offset_y) > kBoundaryEpsilon ||
      qAbs(source_track->group_transform_rotation_degrees) > kBoundaryEpsilon ||
      qAbs(target_track->group_transform_scale - 1) > kBoundaryEpsilon ||
      qAbs(target_track->group_transform_rotation_degrees) > kBoundaryEpsilon)
    return reject(QStringLiteral("Tracked placement currently requires an untransformed Fit source and a target without group scale/rotation."));
  const auto samples = source->motion_tracking.value(QStringLiteral("samples")).toList();
  if (samples.size() < 2 || source->motion_tracking.value(QStringLiteral("sourceSignature")).toString() != source->source_identity_signature ||
      media_identity_signature(source->file_path) != source->source_identity_signature || clipHasRetimeCurve(*source))
    return reject(QStringLiteral("The motion track is missing, stale, or has a retime curve. Analyze a constant-speed source again."));
  if (clipHasKeyframesFor(*target, QStringLiteral("frameOffsetX")) || clipHasKeyframesFor(*target, QStringLiteral("frameOffsetY")))
    return reject(QStringLiteral("The target already has position animation. Use a fresh target or remove its position keyframes before attaching."));
  const auto aspect = source->source_size.isValid() ? static_cast<double>(source->source_size.width()) / source->source_size.height()
      : static_cast<double>(sequence_size_.width()) / sequence_size_.height();
  const auto seq_aspect = static_cast<double>(sequence_size_.width()) / sequence_size_.height();
  const auto ratio_x = qMin(1.0, aspect / seq_aspect);
  const auto ratio_y = qMin(1.0, seq_aspect / aspect);
  QVector<ClipKeyframe> keys;
  const auto first = samples.first().toMap();
  const auto first_x = first.value(QStringLiteral("x")).toDouble() + first.value(QStringLiteral("width")).toDouble() * 0.5;
  const auto first_y = first.value(QStringLiteral("y")).toDouble() + first.value(QStringLiteral("height")).toDouble() * 0.5;
  for (const auto& value : samples) {
    const auto sample = value.toMap();
    const auto source_seconds = sample.value(QStringLiteral("sourceSeconds")).toDouble();
    if (source_seconds < source->trim_start_seconds - kBoundaryEpsilon || source_seconds >= source->trim_end_seconds) continue;
    const auto sequence_seconds = source->start_seconds + (source_seconds - source->trim_start_seconds) / source->playback_rate;
    const auto local = sequence_seconds - target->start_seconds;
    if (local < -kBoundaryEpsilon || local >= clipTrimmedDuration(*target)) continue;
    const auto dx = (sample.value(QStringLiteral("x")).toDouble() + sample.value(QStringLiteral("width")).toDouble() * 0.5 - first_x) * ratio_x * 4;
    const auto dy = (sample.value(QStringLiteral("y")).toDouble() + sample.value(QStringLiteral("height")).toDouble() * 0.5 - first_y) * ratio_y * 4;
    if (!std::isfinite(dx) || !std::isfinite(dy) || qAbs(target->frame_offset_x + dx) > 4 || qAbs(target->frame_offset_y + dy) > 4)
      return reject(QStringLiteral("The tracked placement exceeds the supported position range."));
    ClipKeyframe x(QStringLiteral("frameOffsetX"), qMax(0.0, local), target->frame_offset_x + dx, QStringLiteral("linear"));
    ClipKeyframe y(QStringLiteral("frameOffsetY"), qMax(0.0, local), target->frame_offset_y + dy, QStringLiteral("linear"));
    x.source = y.source = QStringLiteral("motion-track:%1").arg(source_clip_id);
    keys.append(x); keys.append(y);
  }
  if (keys.size() < 4) return reject(QStringLiteral("The target must overlap at least two tracked source frames."));
  pushUndoSnapshot();
  target->keyframes.append(keys);
  std::sort(target->keyframes.begin(), target->keyframes.end(), [](const ClipKeyframe& a, const ClipKeyframe& b) {
    return a.parameter == b.parameter ? a.local_seconds < b.local_seconds : a.parameter < b.parameter;
  });
  emitAllState();
  setDirty(true);
  setStatus(QStringLiteral("Attached motion as %1 editable position keyframes. Undo restores the target.").arg(keys.size()));
  return true;
}
