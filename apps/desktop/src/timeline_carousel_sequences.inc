bool TimelineDocumentController::openCarouselBoardSequence(QObject* object, int index, const QVariantList& media) {
  const auto access = customer_edit_access_.enter();
  auto* carousel = qobject_cast<CarouselDocumentController*>(object);
  if (!access || !carousel || !carousel_board_session_.isEmpty() || timelineSaveInProgress() || insideCompound()) {
    setStatus(QStringLiteral("Finish the current Timeline operation before opening a board sequence.")); return false;
  }
  const auto prepared = carousel->prepareBoardSequence(index);
  if (prepared.isEmpty()) { setStatus(carousel->status()); return false; }
  const auto directory = prepared.value(QStringLiteral("directory")).toString();
  QVariantList admitted_media;
  for (const auto& value : media) {
    auto asset = value.toMap();
    const auto path = asset.value(QStringLiteral("filePath")).toString();
    auto duration = asset.value(QStringLiteral("durationSeconds")).toDouble();
    if (duration <= 0) duration = probed_media_duration_seconds(path);
    if (!QFileInfo(path).isFile() || !std::isfinite(duration) || duration <= 0) {
      setStatus(QStringLiteral("The selected video could not be read. Reconnect or relink it in Library.")); return false;
    }
    asset.insert(QStringLiteral("durationSeconds"), duration);
    admitted_media.push_back(asset);
  }
  carousel_parent_frame_.reset();
  carousel_parent_dirty_ = dirty_;
  if (has_active_timeline_) {
    CompoundNavigationFrame frame;
    frame.parent_snapshot_path = QDir(directory).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral("-previous.json"));
    frame.parent_saved_document_path = saved_document_path_;
    frame.parent_saved_render_path = saved_render_path_;
    frame.parent_saved_poster_path = saved_poster_path_;
    frame.parent_undo_stack = undo_stack_;
    frame.parent_redo_stack = redo_stack_;
    if (!persistDocument(frame.parent_snapshot_path)) { setStatus(QStringLiteral("Unable to preserve the open Timeline.")); return false; }
    carousel_parent_frame_ = std::move(frame);
  }
  carousel_board_owner_ = carousel;
  carousel_board_session_ = prepared;
  setDirty(false);  // The complete previous sequence is checkpointed above.
  const auto existing = prepared.value(QStringLiteral("sequencePath")).toString();
  bool loaded = false;
  const auto previous_id = timeline_id_;
  if (!existing.isEmpty()) {
    QFile file(existing);
    loaded = file.open(QIODevice::ReadOnly) && loadDocumentObject(QJsonDocument::fromJson(file.readAll()).object(), existing, false);
  }
  else {
    createTimelineWithSettings({{QStringLiteral("timelineName"), prepared.value(QStringLiteral("name")).toString() + QStringLiteral(" · Video")},
        {QStringLiteral("width"), prepared.value(QStringLiteral("width"))}, {QStringLiteral("height"), prepared.value(QStringLiteral("height"))},
        {QStringLiteral("frameRate"), 30}, {QStringLiteral("projectBinding"), prepared.value(QStringLiteral("projectName"))},
        {QStringLiteral("exportContainer"), QStringLiteral("mp4")}, {QStringLiteral("exportCodec"), QStringLiteral("h264")}});
    loaded = has_active_timeline_ && timeline_id_ != previous_id;
  }
  if (!loaded) { restoreCarouselParentTimeline(); return false; }
  if (!prepared.value(QStringLiteral("sourceBoardId")).toString().isEmpty() &&
      prepared.value(QStringLiteral("sourceBoardId")) != prepared.value(QStringLiteral("boardId")))
    timeline_id_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
  sequence_size_ = QSize(prepared.value(QStringLiteral("width")).toInt(), prepared.value(QStringLiteral("height")).toInt());
  QString video_track;
  double start = existing.isEmpty() ? 0.0 : totalDurationSeconds();
  double added_duration = 0.0;
  for (const auto& value : admitted_media) added_duration += value.toMap().value(QStringLiteral("durationSeconds")).toDouble();
  const auto composition = prepared.value("composition").toList();
  if (!composition.isEmpty()) {
    const auto old_tracks = tracks_;
    QSet<QString> claimed;
    QHash<QString, int> link_counts;
    for (const auto& value : prepared.value("videoLayers").toList()) {
      const auto id = value.toMap().value("boardVideoSource").toMap().value("clipId").toString();
      if (!id.isEmpty()) ++link_counts[id];
    }
    QVariantMap clip_layer_ids;
    QVector<Track> rebuilt;
    int lane = 0;
    for (const auto& value : composition) {
      const auto part = value.toMap();
      const auto is_video = part.value("kind").toString() == "video";
      const auto layer = part.value("layer").toMap();
      const auto source = layer.value("boardVideoSource").toMap();
      const auto layer_id = layer.value("id").toString();
      const auto track_id = QStringLiteral("carousel-composition:") + part.value("key").toString();
      Track picture; picture.track_id = track_id; picture.kind = TrackKind::Video;
      Track audio; audio.kind = TrackKind::Audio; audio.track_id = track_id + ":audio";
      std::optional<Clip> previous;
      if (is_video) for (const auto& track : old_tracks) if (track.kind == TrackKind::Video) for (const auto& clip : track.clips) {
        if ((!source.value("clipId").toString().isEmpty() && clip.clip_id == source.value("clipId").toString() &&
             (link_counts.value(clip.clip_id) == 1 || clip.asset_id == QStringLiteral("carousel-video-layer:") + layer_id)) ||
            (source.value("clipId").toString().isEmpty() && clip.asset_id == QStringLiteral("carousel-video-layer:") + layer_id)) {
          if (claimed.contains(clip.clip_id)) continue;
          previous = clip; picture = track; picture.clips.clear(); picture.track_id = track_id;
          for (const auto& paired : old_tracks) if (paired.kind == TrackKind::Audio && paired.lane_index == track.lane_index) {
            audio = paired; audio.clips.clear(); audio.track_id = track_id + ":audio";
          }
          claimed.insert(clip.clip_id);
        }
      }
      picture.lane_index = lane; audio.lane_index = lane++;
      picture.targeted = is_video; audio.targeted = is_video;
      picture.display_label = is_video ? layer.value("name").toString() : part.value("name").toString();
      picture.visible = !is_video || layer.value("enabled", true).toBool();
      picture.locked = false;
      picture.muted = is_video && !picture.visible;
      if (previous) picture.clips.push_back(*previous);
      rebuilt.push_back(picture); rebuilt.push_back(audio);
    }
    // Preserve additional editorial lanes and clips authored in Timeline.
    for (const auto& track : old_tracks) if (track.kind == TrackKind::Video && !track.track_id.startsWith("carousel-composition:")) {
      auto picture = track;
      picture.clips.erase(std::remove_if(picture.clips.begin(), picture.clips.end(), [&](const auto& clip) {
        return claimed.contains(clip.clip_id) || clip.asset_id.startsWith("carousel-background:") ||
            clip.asset_id.startsWith("carousel-video-layer:") || clip.asset_id.startsWith("carousel-static:");
      }), picture.clips.end());
      if (picture.clips.isEmpty()) continue;
      picture.lane_index = lane; rebuilt.push_back(picture);
      for (const auto& paired : old_tracks) if (paired.kind == TrackKind::Audio && paired.lane_index == track.lane_index) {
        auto audio = paired; audio.lane_index = lane; rebuilt.push_back(audio);
      }
      ++lane;
    }
    tracks_ = rebuilt;
    for (const auto& value : composition) {
      const auto part = value.toMap(); const auto is_video = part.value("kind").toString() == "video";
      const auto layer = part.value("layer").toMap(); const auto source = layer.value("boardVideoSource").toMap();
      const auto track_id = QStringLiteral("carousel-composition:") + part.value("key").toString();
      auto track = std::find_if(tracks_.begin(), tracks_.end(), [&](const auto& candidate) { return candidate.track_id == track_id; });
      if (track == tracks_.end()) { restoreCarouselParentTimeline(); return false; }
      if (track->clips.isEmpty()) {
        const auto path = is_video ? source.value("filePath").toString() : part.value("filePath").toString();
        const auto duration = is_video ? probed_media_duration_seconds(path) : qMax(5.0, totalDurationSeconds());
        if (!QFileInfo(path).isFile() || duration <= 0) { setStatus(QStringLiteral("Reconnect the board's original media in Library.")); restoreCarouselParentTimeline(); return false; }
        const auto added = createTimelineClipForAutomation(track_id,
            (is_video ? QStringLiteral("carousel-video-layer:") : QStringLiteral("carousel-static:")) + part.value("key").toString(),
            path, track->display_label, duration, 0, duration, 0);
        if (!added.value("ok").toBool()) { setStatus(added.value("error").toString()); restoreCarouselParentTimeline(); return false; }
      }
      // Creation can reorder tracks while synthesizing the paired audio rows.
      track = std::find_if(tracks_.begin(), tracks_.end(), [&](const auto& candidate) { return candidate.track_id == track_id; });
      auto& clip = track->clips.front();
      if (is_video) {
        clip_layer_ids.insert(clip.clip_id, layer.value("id"));
        const auto frame = board_video_frame(layer, sequence_size_);
        if (frame.isEmpty()) { setStatus(QStringLiteral("The video's layout frame is invalid.")); restoreCarouselParentTimeline(); return false; }
        if (!claimed.contains(clip.clip_id) || !source.contains("boardFrame") || source.value("boardFrame").toMap() != frame) {
          clip.frame_mode = frame.value("frameMode").toString();
          clip.frame_width = frame.value("frameWidth").toDouble(); clip.frame_height = frame.value("frameHeight").toDouble();
          clip.frame_scale = 1; clip.frame_offset_x = frame.value("frameOffsetX").toDouble(); clip.frame_offset_y = frame.value("frameOffsetY").toDouble();
          clip.frame_rotation_degrees = frame.value("frameRotationDegrees").toDouble();
          clip.source_crop_left = frame.value("sourceCropLeft").toDouble(); clip.source_crop_top = frame.value("sourceCropTop").toDouble();
          clip.source_crop_right = frame.value("sourceCropRight").toDouble(); clip.source_crop_bottom = frame.value("sourceCropBottom").toDouble();
          clip.keyframes.erase(std::remove_if(clip.keyframes.begin(), clip.keyframes.end(), [](const auto& key) {
            return key.parameter == "frameScale" || key.parameter == "frameOffsetX" || key.parameter == "frameOffsetY" || key.parameter == "frameRotationDegrees";
          }), clip.keyframes.end());
        }
        clip.opacity = layer.value("opacity", 1.0).toDouble();
        track->locked = layer.value("locked").toBool();
      } else { clip.frame_mode = "stretch"; track->locked = true; }
    }
    carousel_board_session_.insert("usesBoardLayers", true);
    carousel_board_session_.insert("clipLayerIds", clip_layer_ids);
    syncAudioTracks();
  } else if (existing.isEmpty()) {
    const auto background_duration = qMax(0.1, added_duration > 0 ? added_duration : 5.0);
    const auto background = createTimelineClipForAutomation(tracks_.front().track_id,
        QStringLiteral("carousel-background:") + prepared.value(QStringLiteral("boardId")).toString(),
        prepared.value(QStringLiteral("backgroundPath")).toString(), QStringLiteral("Canvas background"),
        background_duration, 0, background_duration, 0);
    if (!background.value(QStringLiteral("ok")).toBool()) { setStatus(background.value(QStringLiteral("error")).toString()); restoreCarouselParentTimeline(); return false; }
  }
  if ((composition.isEmpty() && existing.isEmpty()) || !admitted_media.isEmpty()) {
    appendTrackPair();
    for (auto track = tracks_.rbegin(); track != tracks_.rend(); ++track) if (track->kind == TrackKind::Video) {
      video_track = track->track_id; track->display_label = QStringLiteral("Board video"); break;
    }
    if (video_track.isEmpty()) { restoreCarouselParentTimeline(); return false; }
    for (auto& track : tracks_) if (track.kind == TrackKind::Video) track.targeted = track.track_id == video_track;
    for (const auto& value : admitted_media) {
      const auto asset = value.toMap();
      const auto duration = qMax(0.1, asset.value(QStringLiteral("durationSeconds"), 5.0).toDouble());
      const auto added = createTimelineClipForAutomation(video_track, asset.value(QStringLiteral("assetId")).toString(),
          asset.value(QStringLiteral("filePath")).toString(), asset.value(QStringLiteral("cameraLabel")).toString(), duration, 0, duration, start);
      if (!added.value(QStringLiteral("ok")).toBool()) { setStatus(added.value(QStringLiteral("error")).toString()); restoreCarouselParentTimeline(); return false; }
      setSelectedFrameMode(QStringLiteral("fit"));
      setSelectedClipTransform(0, 0, 0.8, 0);
      start += duration;
    }
  }
  sequence_size_ = QSize(prepared.value(QStringLiteral("width")).toInt(), prepared.value(QStringLiteral("height")).toInt());
  double content_end = 0;
  for (const auto& track : tracks_) if (track.kind == TrackKind::Video) for (const auto& clip : track.clips)
    if (!clip.asset_id.startsWith("carousel-background:") && !clip.asset_id.startsWith("carousel-static:"))
      content_end = qMax(content_end, clip.start_seconds + clipTrimmedDuration(clip));
  if (content_end > 0) for (auto& track : tracks_) for (auto& clip : track.clips)
    if (clip.asset_id.startsWith("carousel-background:") || clip.asset_id.startsWith("carousel-static:")) {
      clip.start_seconds = 0; clip.duration_seconds = content_end; clip.trim_start_seconds = 0; clip.trim_end_seconds = content_end;
    }
  for (auto& track : tracks_) for (auto& clip : track.clips) if (clip.asset_id.startsWith(QStringLiteral("carousel-background:"))) {
    clip.asset_id = QStringLiteral("carousel-background:") + prepared.value(QStringLiteral("boardId")).toString();
    clip.file_path = prepared.value(QStringLiteral("backgroundPath")).toString();
    clip.source_size = sequence_size_;
    clip.source_file_size = QFileInfo(clip.file_path).size();
    clip.source_identity_signature = media_identity_signature(clip.file_path);
    clip.source_content_sha256.clear();
    clip.source_integrity_status = QStringLiteral("pending");
    clip.poster_source = QUrl::fromLocalFile(clip.file_path).toString();
  }
  const auto draft = QUuid::createUuid().toString(QUuid::WithoutBraces);
  saved_document_path_ = QDir(directory).filePath(draft + QStringLiteral("-draft.json"));
  saved_render_path_ = QDir(directory).filePath(draft + QStringLiteral("-draft.mp4"));
  saved_poster_path_ = QDir(directory).filePath(draft + QStringLiteral("-draft.png"));
  carousel_board_session_.insert(QStringLiteral("draftPath"), saved_document_path_);
  // Rebuilding canvas slices can select a locked still. Enter the sequence on
  // an editable movie so trimming and Program transforms are available.
  const auto* selected = clipAt(selectedClipRef());
  const auto* selected_track = trackAt(selectedClipRef().track_index);
  if (!selected || selected->media_type != "video" || !selected_track || selected_track->locked) {
    const auto rows = clips();
    for (int row = 0; row < rows.size(); ++row) {
      const auto clip = rows.at(row).toMap();
      const auto ref = clipRefAt(row); const auto* track = trackAt(ref.track_index);
      if (clip.value("mediaType").toString() == "video" && track && track->kind == TrackKind::Video && !track->locked && track->visible) { selectClip(row); break; }
    }
  }
  if (!persistDocument(saved_document_path_)) { restoreCarouselParentTimeline(); return false; }
  setDirty(true);
  emit savedPathsChanged();
  emitAllState();
  emit carouselBoardSequenceChanged();
  setStatus(QStringLiteral("Trim video on the Timeline and move or resize it in the Program viewer. Apply saves it to this board."));
  return true;
}

bool TimelineDocumentController::applyCarouselBoardSequence() {
  const auto access = customer_edit_access_.enter();
  if (!access || carousel_board_session_.isEmpty() || carousel_board_render_watcher_) return false;
  if (mediaAnalysisBusy()) { setStatus(QStringLiteral("Wait for video and audio analysis before applying this board.")); return false; }
  auto* carousel = qobject_cast<CarouselDocumentController*>(carousel_board_owner_.data());
  if (!carousel || carousel->savedDocumentId() != carousel_board_session_.value(QStringLiteral("documentId")).toString()) {
    setStatus(QStringLiteral("The target carousel changed. Return to the original board before applying.")); return false;
  }
  const auto board_id = carousel_board_session_.value(QStringLiteral("boardId")).toString();
  const auto background_id = QStringLiteral("carousel-background:") + board_id;
  double content_end = 0;
  for (const auto& track : tracks_) for (const auto& clip : track.clips)
    if (clip.asset_id != background_id && !clip.asset_id.startsWith("carousel-static:")) content_end = qMax(content_end, clip.start_seconds + clipTrimmedDuration(clip));
  if (content_end <= 0) { setStatus(QStringLiteral("Add video or other media to this board sequence before applying.")); return false; }
  for (auto& track : tracks_) for (auto& clip : track.clips) if (clip.asset_id == background_id || clip.asset_id.startsWith("carousel-static:")) {
    clip.start_seconds = 0; clip.duration_seconds = content_end; clip.trim_start_seconds = 0; clip.trim_end_seconds = content_end;
  }
  const auto directory = carousel_board_session_.value(QStringLiteral("directory")).toString();
  const auto revision = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto project = QDir(directory).filePath(revision + QStringLiteral(".json"));
  const auto output = QDir(directory).filePath(revision + QStringLiteral(".mp4"));
  const auto poster = QDir(directory).filePath(revision + QStringLiteral(".png"));
  if (!persistDocument(project)) { setStatus(QStringLiteral("The editable board sequence could not be saved.")); return false; }
  const auto edit_revision = document_edit_revision_;
  auto receipt = carousel_board_session_;
  QVariantList video_layers;
  const auto original_layers = carousel_board_session_.value("videoLayers").toList();
  QSet<QString> used_layers;
  for (const auto& track : tracks_) if (track.kind == TrackKind::Video) for (const auto& clip : track.clips) {
    if (clip.media_type != "video") continue;
    QVariantMap layer;
    for (const auto& value : original_layers) {
      const auto candidate = value.toMap(); const auto source = candidate.value("boardVideoSource").toMap();
      if (used_layers.contains(candidate.value("id").toString())) continue;
      const auto mapped_id = carousel_board_session_.value("clipLayerIds").toMap().value(clip.clip_id).toString();
      if ((!mapped_id.isEmpty() && mapped_id == candidate.value("id").toString()) ||
          (mapped_id.isEmpty() && (source.value("clipId").toString() == clip.clip_id || clip.asset_id == "carousel-video-layer:" + candidate.value("id").toString()))) { layer = candidate; break; }
    }
    auto poster_layer = board_video_source_layer(clip.file_path, clip.display_label, clip.asset_id, clip.trim_start_seconds);
    if (poster_layer.isEmpty()) { setStatus(QStringLiteral("The video's selected frame could not be read. Reconnect the original media.")); return false; }
    if (layer.isEmpty()) layer = poster_layer;
    else layer.insert("sourcePath", poster_layer.value("sourcePath"));
    QVariantMap geometry{{"frameMode", clip.frame_mode}, {"frameWidth", clip.frame_width}, {"frameHeight", clip.frame_height},
        {"frameScale", clip.frame_scale}, {"frameOffsetX", clip.frame_offset_x}, {"frameOffsetY", clip.frame_offset_y},
        {"frameRotationDegrees", clip.frame_rotation_degrees}, {"sourceCropLeft", clip.source_crop_left},
        {"sourceCropTop", clip.source_crop_top}, {"sourceCropRight", clip.source_crop_right}, {"sourceCropBottom", clip.source_crop_bottom}, {"opacity", clip.opacity}};
    layer = board_video_layer_from_clip(layer, geometry, sequence_size_);
    if (layer.isEmpty()) { setStatus(QStringLiteral("The video's frame cannot be represented on this board.")); return false; }
    layer.insert("enabled", track.visible); layer.insert("locked", track.locked);
    layer.insert("canvasWidth", sequence_size_.width()); layer.insert("canvasHeight", sequence_size_.height());
    auto source = layer.value("boardVideoSource").toMap();
    source.insert("clipId", clip.clip_id); source.insert("trimStartSeconds", clip.trim_start_seconds);
    source.insert("trimEndSeconds", clip.trim_end_seconds); source.insert("startSeconds", clip.start_seconds);
    source.insert("boardFrame", board_video_frame(layer, sequence_size_));
    layer.insert("boardVideoSource", source);
    used_layers.insert(layer.value("id").toString()); video_layers.push_back(layer);
  }
  receipt.insert("videoLayers", video_layers);
  receipt.insert("usesBoardLayers", true);
  receipt.insert(QStringLiteral("sequencePath"), project);
  receipt.insert(QStringLiteral("videoPath"), output);
  receipt.insert(QStringLiteral("posterPath"), poster);
  receipt.insert(QStringLiteral("width"), sequence_size_.width());
  receipt.insert(QStringLiteral("height"), sequence_size_.height());
  receipt.insert(QStringLiteral("durationSeconds"), content_end);
  carousel_board_session_.insert(QStringLiteral("busy"), true);
  carousel_board_session_.remove(QStringLiteral("error"));
  emit carouselBoardSequenceChanged();
  carousel_board_cancel_ = std::make_shared<std::atomic_bool>(false);
  auto* watcher = new QFutureWatcher<QVariantMap>(this);
  carousel_board_render_watcher_ = watcher;
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, receipt, edit_revision]() {
    const auto result = watcher->result();
    watcher->deleteLater(); carousel_board_render_watcher_ = nullptr;
    carousel_board_session_.insert(QStringLiteral("busy"), false);
    if (carousel_board_cancel_ && carousel_board_cancel_->load(std::memory_order_relaxed)) {
      if (restoreCarouselParentTimeline()) emit carouselBoardSequenceClosed(false);
      return;
    }
    auto* owner = qobject_cast<CarouselDocumentController*>(carousel_board_owner_.data());
    const auto valid = result.value(QStringLiteral("ok")).toBool() && owner && document_edit_revision_ == edit_revision &&
        owner->attachBoardSequence(receipt.value(QStringLiteral("documentId")).toString(), receipt.value(QStringLiteral("boardId")).toString(), receipt);
    if (!valid) {
      const auto error = !result.value(QStringLiteral("ok")).toBool() ? result.value(QStringLiteral("error")).toString()
          : !owner ? QStringLiteral("The carousel closed before rendering finished.")
          : document_edit_revision_ != edit_revision ? QStringLiteral("The sequence changed while rendering. Apply again to render the latest edit.") : owner->status();
      carousel_board_session_.insert(QStringLiteral("error"), error);
      setStatus(error); emit carouselBoardSequenceChanged(); return;
    }
    if (restoreCarouselParentTimeline()) emit carouselBoardSequenceClosed(true);
  });
  const auto cancellation = carousel_board_cancel_;
  watcher->setFuture(QtConcurrent::run([project, output, poster, cancellation]() -> QVariantMap {
    TimelineDocumentController renderer;
    renderer.setRenderCancellationToken(cancellation);
    if (!renderer.loadTimeline(project) || !renderer.renderCurrentTimeline(output))
      return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The board sequence could not render. Check its media and try Apply again.")}};
    const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    QProcess process;
    process.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-i"), output, QStringLiteral("-frames:v"), QStringLiteral("1"), poster});
    if (!process.waitForFinished(30000) || process.exitCode() != 0 || !QFileInfo::exists(poster))
      return {{QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("The video rendered, but its board preview could not be created. Try Apply again.")}};
    return {{QStringLiteral("ok"), true}};
  }));
  return true;
}

bool TimelineDocumentController::restoreCarouselParentTimeline() {
  if (carousel_parent_frame_) {
    const auto frame = *carousel_parent_frame_;
    compound_navigation_loading_ = true;
    QFile snapshot(frame.parent_snapshot_path);
    const auto restored = snapshot.open(QIODevice::ReadOnly) &&
        loadDocumentObject(QJsonDocument::fromJson(snapshot.readAll()).object(), frame.parent_snapshot_path, false);
    compound_navigation_loading_ = false;
    if (!restored) { setStatus(QStringLiteral("The board was saved, but the previous Timeline could not be restored. Your snapshot is preserved.")); return false; }
    saved_document_path_ = frame.parent_saved_document_path;
    saved_render_path_ = frame.parent_saved_render_path;
    saved_poster_path_ = frame.parent_saved_poster_path;
    undo_stack_ = frame.parent_undo_stack; redo_stack_ = frame.parent_redo_stack;
    setDirty(carousel_parent_dirty_);
    emit savedPathsChanged(); emit undoStateChanged();
  } else closeTimeline();
  carousel_board_session_.clear(); carousel_board_owner_.clear(); carousel_parent_frame_.reset();
  emit carouselBoardSequenceChanged();
  return true;
}

bool TimelineDocumentController::cancelCarouselBoardSequence() {
  const auto customer_edit_scope = customer_edit_access_.restore();
  if (carousel_board_session_.isEmpty()) return false;
  if (carousel_board_render_watcher_) {
    if (carousel_board_cancel_) carousel_board_cancel_->store(true, std::memory_order_relaxed);
    setStatus(QStringLiteral("Stopping the board render…"));
    return true;
  }
  if (!persistDocument(carousel_board_session_.value(QStringLiteral("draftPath")).toString())) return false;
  if (!restoreCarouselParentTimeline()) return false;
  emit carouselBoardSequenceClosed(false);
  return true;
}
