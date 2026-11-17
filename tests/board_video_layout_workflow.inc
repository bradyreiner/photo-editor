void verify_board_video_layout(const QTemporaryDir& runtime) {
  const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
  const auto run = [&](const QStringList& args) {
    QProcess process; process.start(ffmpeg, args);
    require(process.waitForFinished(60000) && process.exitCode() == 0, "Generate or decode layout video fixture");
  };
  const auto wait = [](const std::function<bool()>& ready) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < 60000) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(3); }
    return ready();
  };
  QImage early(120, 240, QImage::Format_RGB32), late(120, 240, QImage::Format_RGB32);
  for (int y = 0; y < 240; ++y) for (int x = 0; x < 120; ++x) {
    early.setPixelColor(x, y, y < 80 ? Qt::red : y < 160 ? Qt::green : Qt::blue);
    late.setPixelColor(x, y, y < 80 ? Qt::yellow : y < 160 ? Qt::magenta : Qt::cyan);
  }
  const auto early_path = runtime.filePath("video-layout-early.png"), late_path = runtime.filePath("video-layout-late.png");
  require(early.save(early_path) && late.save(late_path), "Save spatial video fixtures");
  const auto video_path = runtime.filePath("video-layout-source.mp4");
  run({"-y", "-loop", "1", "-t", "2", "-i", early_path, "-loop", "1", "-t", "2", "-i", late_path,
      "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v]", "-map", "[v]", "-r", "30", "-c:v", "libx264", "-pix_fmt", "yuv420p", video_path});
  QFile original(video_path); require(original.open(QIODevice::ReadOnly), "Read full video source");
  const auto original_bytes = original.readAll(); original.close();
  QImage photo(240, 80, QImage::Format_RGB32); photo.fill(QColor(80, 80, 80));
  const auto photo_path = runtime.filePath("video-layout-photo.png"); require(photo.save(photo_path), "Save mixed-layout photo");
  CarouselDocumentController boards;
  require(boards.createEditCanvas({{"name", "Mixed media layout"}, {"width", 240}, {"height", 164}, {"background", "transparent"}}), "Create mixed layout canvas");
  auto payload = boards.editBoardPayload(0);
  EditorPreviewController editor;
  const auto load = [&] {
    payload = boards.editBoardPayload(0);
    editor.loadAsset("mixed-video-layout", payload.value("filePath").toString(), {}, {}, {}, {}, payload.value("adjustmentPatchJson").toString(), true); settle(editor);
  };
  load();
  require(editor.addSourceLayer(photo_path, "Photo") && editor.addBoardVideoLayer(video_path, "Portrait video", "original-video"), "Import photo and full-resolution portrait video as separate layers"); settle(editor);
  require(editor.applyBoardPhotoLayout({0, 1}, "stack", {{"border", 4}, {"margin", 0}, {"spacing", 0}, {"displayReferredCanvas", true}}), "Apply the same equal-halves layout and uniform border to photo and video"); settle(editor);
  require(editor.setBoardPhotoFraming(0, .5, 1, 1), "Reframe the video's lower original pixels inside its layout frame"); settle(editor);
  const auto before = editor.currentAssetBoundAdjustmentPatchJson();
  const auto layer = editor.photoLayers().front().toMap();
  require(layer.value("pixelWidth").toInt() == 120 && layer.value("pixelHeight").toInt() == 240 &&
      layer.value("boardVideoSource").toMap().value("filePath") == video_path, "Video layout retains every original source pixel and file identity");
  const auto bounds = editor.photoLayerBounds(0);
  require(qAbs(bounds.value("width").toDouble() - 232) < .01 && qAbs(bounds.value("height").toDouble() - 76) < .01, "Video fills exactly one layout half without stretching");
  require(boards.applyAdjustmentPatchToPhotoEditTarget(payload.value("targetKey").toString(), before), "Persist the mixed layout recipe");
  require(boards.boardSequence(0).value("needsUpdate").toBool(), "Unrendered video layers require a sequence before export");
  TimelineDocumentController timeline;
  require(timeline.openCarouselBoardSequence(&boards, 0) && wait([&] { return !timeline.mediaAnalysisBusy(); }), "Open the mixed layout in its board-owned Timeline");
  const auto select_video = [&] {
    const auto clips = timeline.clips();
    for (int i = 0; i < clips.size(); ++i) if (clips.at(i).toMap().value("filePath") == video_path && clips.at(i).toMap().value("mediaType") == "video") { timeline.selectClip(i); return true; }
    return false;
  };
  require(select_video(), "Select the actual movie rather than a poster or flattened background");
  const auto frame = timeline.currentClip();
  require(qAbs(frame.value("frameWidth").toDouble() * 240 - 232) < .01 &&
      qAbs(frame.value("frameHeight").toDouble() * 164 - 76) < .01 && frame.value("sourceCropTop").toDouble() > .8,
      "Timeline receives the board's exact layout frame and non-destructive source window");
  timeline.setSelectedTrimStartSeconds(.5); timeline.setSelectedTrimEndSeconds(2.5);
  require(timeline.moveSelectedClipToTrackRowWithMode(timeline.currentClip().value("trackRowIndex").toInt(), .5, "overwrite", false), "Position the clip in time independently of its spatial frame");
  bool applied = false;
  QObject::connect(&timeline, &TimelineDocumentController::carouselBoardSequenceClosed, &timeline, [&](bool ok) { applied = ok; });
  const auto apply = [&] {
    applied = false;
    require(timeline.applyCarouselBoardSequence(), "Apply mixed media sequence");
    require(wait([&] { return timeline.carouselBoardSession().isEmpty() || !timeline.carouselBoardSession().value("busy").toBool(); }), "Finish rendering mixed media sequence");
    if (!applied) std::cerr << timeline.status().toStdString() << '\n';
    require(applied, "Attach the actual rendered mixed-layout movie");
  };
  apply();
  const auto decode = [&](double seconds, const QString& name) {
    const auto path = runtime.filePath(name);
    run({"-y", "-ss", QString::number(seconds), "-i", boards.boardSequence(0).value("videoPath").toString(), "-frames:v", "1", path});
    return QImage(path);
  };
  const auto first = decode(.7, "layout-first.png"), last = decode(2.3, "layout-last.png"), before_start = decode(.1, "layout-before-start.png");
  const auto proof = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
  if (!proof.isEmpty()) {
    QDir().mkpath(proof); first.save(QDir(proof).filePath("mixed-video-layout-first.png")); last.save(QDir(proof).filePath("mixed-video-layout-late.png"));
    for (const auto& entry : {qMakePair(QStringLiteral("videoPath"), QStringLiteral("mixed-video-layout.mp4")),
                            qMakePair(QStringLiteral("sequencePath"), QStringLiteral("mixed-video-layout.json"))}) {
      QFile source(boards.boardSequence(0).value(entry.first).toString()), destination(QDir(proof).filePath(entry.second));
      require(source.open(QIODevice::ReadOnly) && destination.open(QIODevice::WriteOnly) && destination.write(source.readAll()) >= 0, "Preserve the actual rendered movie and sequence as review artifacts");
    }
  }
  std::cout << "Mixed frame " << first.width() << 'x' << first.height() << " first=" << first.pixelColor(120, 40).name().toStdString()
            << " late=" << last.pixelColor(120, 40).name().toStdString() << '\n';
  const auto white = [](QColor c) { return c.red() > 235 && c.green() > 235 && c.blue() > 235; };
  require(first.size() == QSize(240, 164) && first.pixelColor(120, 40).blue() > 200 && first.pixelColor(120, 40).blue() > first.pixelColor(120, 40).red() * 4,
      "Export shows the reframed lower part of the moving video inside its half");
  require(last.pixelColor(120, 40).green() > 200 && last.pixelColor(120, 40).blue() > 200,
      "The layout contains temporal video changes, not a still editing proxy");
  require(white(before_start.pixelColor(120, 40)), "A delayed video does not leave a ghost poster baked into the background");
  for (const auto point : {QPoint(0, 40), QPoint(3, 40), QPoint(236, 40), QPoint(120, 0), QPoint(120, 3), QPoint(120, 80), QPoint(120, 83), QPoint(120, 160), QPoint(120, 163)})
    require(white(first.pixelColor(point)), "White video layout borders reach the canvas edge and retain one shared gutter");
  require(first.pixelColor(120, 120).red() > 65 && first.pixelColor(120, 120).red() < 95, "The independent photo retains its layout and developed pixels");
  const auto applied_recipe = boards.editBoardPayload(0).value("adjustmentPatchJson").toString();
  boards.undo(); require(boards.editBoardPayload(0).value("adjustmentPatchJson").toString() == before, "Undo restores the exact pre-sequence media layout recipe");
  boards.redo(); require(boards.editBoardPayload(0).value("adjustmentPatchJson").toString() == applied_recipe, "Redo restores the video geometry, trims and poster atomically");
  load();
  if (editor.currentAssetBoundAdjustmentPatchJson() != applied_recipe && !proof.isEmpty()) {
    QFile saved(QDir(proof).filePath("applied-board-recipe.json")); require(saved.open(QIODevice::WriteOnly), "Write applied recipe diagnostic"); saved.write(applied_recipe.toUtf8());
    QFile loaded(QDir(proof).filePath("reloaded-board-recipe.json")); require(loaded.open(QIODevice::WriteOnly), "Write loaded recipe diagnostic"); loaded.write(editor.currentAssetBoundAdjustmentPatchJson().toUtf8());
  }
  require(editor.currentAssetBoundAdjustmentPatchJson() == applied_recipe, "Applying and loading a video board does not immediately invalidate its render");
  const auto old_layer = editor.photoLayers().front().toMap();
  const auto old_bounds = editor.photoLayerBounds(0), crop = old_layer.value("boardPhotoCrop").toMap();
  const auto resized_scale = old_layer.value("scale").toDouble() * .75;
  require(editor.setPhotoLayerTransform(0,
      qRound(old_bounds.value("x").toDouble() + 12 - crop.value("x").toDouble() * old_layer.value("pixelWidth").toDouble() * resized_scale),
      qRound(old_bounds.value("y").toDouble() + 8 - crop.value("y").toDouble() * old_layer.value("pixelHeight").toDouble() * resized_scale),
      resized_scale, 0), "Directly move and resize video with the ordinary board layer handles"); settle(editor);
  require(editor.setBoardPhotoFraming(0, .5, 0, 1), "Reveal the video's uncropped original top after resizing"); settle(editor);
  require(boards.applyAdjustmentPatchToPhotoEditTarget(payload.value("targetKey").toString(), editor.currentAssetBoundAdjustmentPatchJson()) &&
      boards.boardSequence(0).value("needsUpdate").toBool(), "Spatial edits invalidate playback instead of exporting stale layout pixels");
  const auto updated_bounds = editor.photoLayerBounds(0);
  require(timeline.openCarouselBoardSequence(&boards, 0) && wait([&] { return !timeline.mediaAnalysisBusy(); }) && select_video(), "Reopen the sequence after board reframing and resize");
  const auto updated = timeline.currentClip();
  require(qAbs(updated.value("trimStartSeconds").toDouble() - .5) < .001 && qAbs(updated.value("trimEndSeconds").toDouble() - 2.5) < .001 &&
      qAbs(updated.value("startSeconds").toDouble() - .5) < .001 &&
      qAbs(updated.value("frameWidth").toDouble() * 240 - updated_bounds.value("width").toDouble()) < .01 && updated.value("sourceCropTop").toDouble() < .001,
      "Layout synchronization preserves the exact trims while applying the new size and framing");
  apply();
  const auto resized = decode(.7, "layout-resized.png");
  const auto center = QPoint(qRound(updated_bounds.value("x").toDouble() + updated_bounds.value("width").toDouble() / 2),
      qRound(updated_bounds.value("y").toDouble() + updated_bounds.value("height").toDouble() / 2));
  require(resized.pixelColor(center).red() > 200 && resized.pixelColor(center).red() > resized.pixelColor(center).blue() * 4, "Updated MP4 reveals previously hidden top source pixels in the resized video frame");
  require(white(resized.pixelColor(220, 40)), "Resizing leaves white layout backing rather than an old video poster at the previous bounds");
  require(boards.saveDocument(), "Save mixed-video board");
  CarouselDocumentController reopened;
  require(reopened.loadDocument(boards.savedDocumentId()) && reopened.editBoardPayload(0).value("adjustmentPatchJson") == boards.editBoardPayload(0).value("adjustmentPatchJson"),
      "Save and reopen preserve the exact editable video layer recipe");
  require(original.open(QIODevice::ReadOnly) && original.readAll() == original_bytes, "Layout, resizing, reframing, trimming and exports never rewrite the original movie");
  original.close();
  require(boards.addEditBoard(false), "Create a second board for concurrent video layout");
  payload = boards.editBoardPayload(1);
  editor.loadAsset("two-video-layout", payload.value("filePath").toString(), {}, {}, {}, {}, payload.value("adjustmentPatchJson").toString(), true); settle(editor);
  require(editor.addBoardVideoLayer(video_path, "First instance", "original-video") && editor.addBoardVideoLayer(video_path, "Second instance", "original-video"),
      "Place two independent instances of the same full video source"); settle(editor);
  require(editor.applyBoardPhotoLayout({0, 1}, "columns", {{"border", 4}, {"displayReferredCanvas", true}}) &&
      editor.setBoardPhotoFraming(0, .5, 0, 2) && editor.setBoardPhotoFraming(1, .5, 1, 2), "Arrange and independently reframe two videos side by side"); settle(editor);
  require(boards.applyAdjustmentPatchToPhotoEditTarget(payload.value("targetKey").toString(), editor.currentAssetBoundAdjustmentPatchJson()) &&
      timeline.openCarouselBoardSequence(&boards, 1) && wait([&] { return !timeline.mediaAnalysisBusy(); }), "Open concurrent laid-out videos in one board sequence");
  int videos = 0;
  for (const auto& value : timeline.clips()) {
    const auto clip = value.toMap();
    if (clip.value("mediaType") != "video") continue;
    ++videos; require(clip.value("startSeconds").toDouble() == 0, "Layout videos begin concurrently and retain independent Timeline lanes");
  }
  require(videos == 2, "Both video layout cells retain editable clips");
  apply();
  const auto pair_path = runtime.filePath("two-video-layout.png");
  run({"-y", "-ss", "0.3", "-i", boards.boardSequence(1).value("videoPath").toString(), "-frames:v", "1", pair_path});
  const QImage pair(pair_path);
  require(pair.pixelColor(60, 82).red() > 200 && pair.pixelColor(180, 82).blue() > 200 && white(pair.pixelColor(120, 82)),
      "Both reframed videos render simultaneously in separate cells with one white gutter");
  if (!proof.isEmpty()) pair.save(QDir(proof).filePath("two-video-layout.png"));
  payload = boards.editBoardPayload(1);
  editor.loadAsset("two-video-layout", payload.value("filePath").toString(), {}, {}, {}, {}, payload.value("adjustmentPatchJson").toString(), true); settle(editor);
  require(editor.duplicatePhotoLayer(0) && editor.applyBoardPhotoLayout({0, 1, 2}, "columns", {{"border", 4}, {"displayReferredCanvas", true}}),
      "Duplicate an already linked video layer and include it in the shared layout"); settle(editor);
  QSet<QString> layer_ids;
  for (const auto& value : editor.photoLayers()) if (!value.toMap().value("boardVideoSource").toMap().isEmpty()) layer_ids.insert(value.toMap().value("id").toString());
  require(boards.applyAdjustmentPatchToPhotoEditTarget(payload.value("targetKey").toString(), editor.currentAssetBoundAdjustmentPatchJson()) &&
      timeline.openCarouselBoardSequence(&boards, 1) && wait([&] { return !timeline.mediaAnalysisBusy(); }), "Reopen a board containing a duplicated linked video");
  QSet<QString> clip_ids;
  for (const auto& value : timeline.clips()) if (value.toMap().value("mediaType") == "video") clip_ids.insert(value.toMap().value("clipId").toString());
  require(clip_ids.size() == 3, "Duplicating a video creates a distinct clip and preserves the original instances");
  apply();
  const auto duplicated_recipe = parse_adjustment_payload_or_legacy_recipe(boards.editBoardPayload(1).value("adjustmentPatchJson").toString());
  QSet<QString> applied_ids;
  for (const auto& value : duplicated_recipe.state->layers) if (!value.toMap().value("boardVideoSource").toMap().isEmpty()) applied_ids.insert(value.toMap().value("id").toString());
  require(applied_ids == layer_ids, "Applying duplicated video clips keeps every editable board layer identity stable");
  std::cout << "Mixed video layout: exact frames, uniform white margins, delayed and concurrent motion, direct resize, reframe, trim and timing preservation, duplicate identities, undo and reopen passed\n";
}
