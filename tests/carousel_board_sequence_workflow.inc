void verify_board_video_sequence(const QTemporaryDir& runtime) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  require(!ffmpeg.isEmpty() && !ffprobe.isEmpty(), "Video sequence qualification requires ffmpeg and ffprobe");
  const auto run = [](const QString& program, const QStringList& arguments) {
    QProcess process; process.start(program, arguments);
    if (!process.waitForFinished(60000) || process.exitCode() != 0) {
      std::cerr << process.readAllStandardError().toStdString();
      throw std::runtime_error("Video fixture or inspection command failed");
    }
    return process.readAllStandardOutput();
  };
  const auto wait = [](const std::function<bool()>& ready, int timeout = 60000) {
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(3);
    }
    return ready();
  };
  const auto source = runtime.filePath("sequence-source.mp4");
  run(ffmpeg, {"-y", "-f", "lavfi", "-i", "color=c=red:s=120x80:r=30:d=2", "-f", "lavfi", "-i", "color=c=blue:s=120x80:r=30:d=2",
      "-f", "lavfi", "-i", "sine=frequency=440:sample_rate=48000:duration=4", "-filter_complex", "[0:v][1:v]concat=n=2:v=1:a=0[v]",
      "-map", "[v]", "-map", "2:a", "-c:v", "libx264", "-pix_fmt", "yuv420p", "-c:a", "aac", "-shortest", source});
  CarouselDocumentController boards;
  require(boards.createEditCanvas({{"name", "Video carousel"}, {"width", 200}, {"height", 200},
      {"background", "color"}, {"color", "#008000"}, {"projectName", "Video project"}}), "Create video board with its photo canvas background");
  const auto document_id = boards.savedDocumentId();
  TimelineDocumentController timeline;
  timeline.createTimeline("Existing working timeline"); timeline.addTitleClip();
  const auto parent_id = timeline.agentTaskDocumentId();
  const auto parent_hash = timeline.deterministicDocumentHash();
  const auto parent_dirty = timeline.dirty();
  const auto parent_undo = timeline.canUndo();
  const QVariantList media{QVariantMap{{"assetId", "test-video"}, {"filePath", source}, {"mediaType", "video"}, {"durationSeconds", 4.0}, {"cameraLabel", "Test video"}}};
  require(timeline.openCarouselBoardSequence(&boards, 0, media), "Open an editable sequence owned by this carousel board");
  require(wait([&] { return !timeline.mediaAnalysisBusy(); }), "Finish source video and audio analysis");
  timeline.setSelectedTrimStartSeconds(1.0); timeline.setSelectedTrimEndSeconds(2.5);
  require(timeline.setSelectedClipTransform(0.25, 0.1, 0.4, 0), "Position and resize the video over its photo background");
  bool applied = false;
  QObject::connect(&timeline, &TimelineDocumentController::carouselBoardSequenceClosed, &timeline, [&](bool success) { applied = success; });
  require(timeline.applyCarouselBoardSequence(), "Render and apply the board sequence");
  require(wait([&] { return timeline.carouselBoardSession().isEmpty() || !timeline.carouselBoardSession().value("busy").toBool(); }), "Complete board sequence render");
  if (!applied) std::cerr << timeline.status().toStdString() << '\n';
  require(applied, "A successful native render must attach the sequence to its board");
  require(timeline.agentTaskDocumentId() == parent_id && timeline.deterministicDocumentHash() == parent_hash &&
          timeline.dirty() == parent_dirty && timeline.canUndo() == parent_undo,
          "Returning preserves the previous Timeline's exact document, dirty state, and Undo availability");
  const auto sequence = boards.boardSequence(0);
  const auto video = sequence.value("videoPath").toString();
  const auto probe = QJsonDocument::fromJson(run(ffprobe, {"-v", "error", "-show_streams", "-show_format", "-of", "json", video})).object();
  const auto duration = probe.value("format").toObject().value("duration").toString().toDouble();
  require(qAbs(duration - 1.5) < 0.08, "Trimming the video must also shorten the rendered board sequence");
  bool has_audio = false;
  for (const auto& stream : probe.value("streams").toArray()) if (stream.toObject().value("codec_type") == "audio") has_audio = true;
  require(has_audio, "Video audio remains present after trimming and applying to the carousel");
  const auto pcm = run(ffmpeg, {"-v", "error", "-i", video, "-vn", "-ac", "1", "-ar", "48000", "-f", "f32le", "pipe:1"});
  double energy = 0;
  const auto samples = pcm.size() / static_cast<qsizetype>(sizeof(float));
  for (qsizetype index = 0; index < samples; ++index) {
    float sample = 0;
    std::memcpy(&sample, pcm.constData() + index * static_cast<qsizetype>(sizeof(float)), sizeof(sample));
    energy += double(sample) * sample;
  }
  require(samples > 0 && std::sqrt(energy / double(samples)) > 0.001,
          "The applied video's audio contains the source signal rather than a silent placeholder");
  const auto first_path = runtime.filePath("sequence-first.png"), last_path = runtime.filePath("sequence-last.png");
  run(ffmpeg, {"-y", "-ss", "0.1", "-i", video, "-frames:v", "1", first_path});
  run(ffmpeg, {"-y", "-ss", "1.2", "-i", video, "-frames:v", "1", last_path});
  const QImage first(first_path), last(last_path);
  require(first.size() == QSize(200, 200) && last.size() == first.size(), "The video uses the carousel canvas resolution");
  const auto background = first.pixelColor(5, 5);
  require(background.green() > background.red() + 30 && background.green() > background.blue() + 30, "The board's photo background remains visible around the video");
  QRect red_bounds;
  for (int y = 0; y < first.height(); ++y) for (int x = 0; x < first.width(); ++x) {
    const auto color = first.pixelColor(x, y);
    if (color.red() > 100 && color.red() > color.green() * 2 && color.red() > color.blue() * 2)
      red_bounds = red_bounds.united(QRect(x, y, 1, 1));
  }
  require(!red_bounds.isEmpty() && red_bounds.center().x() > 100 &&
          qAbs(double(red_bounds.width()) / red_bounds.height() - 1.5) < 0.12,
          "Positioning moves the full video and scaling preserves its original aspect ratio");
  const auto end_color = last.pixelColor(red_bounds.center());
  require(end_color.blue() > 100 && end_color.blue() > end_color.red() * 2,
          "The applied board contains moving video from the selected source range, not a still poster");
  boards.undo(); require(boards.boardSequence(0).isEmpty(), "One carousel Undo removes the complete applied video sequence");
  boards.redo(); require(boards.boardSequence(0).value("videoPath") == video, "Redo restores the exact immutable video revision");
  require(boards.addEditBoard(false) && boards.slideCount() == 2 && boards.boardSequence(1).isEmpty(), "The restored plus workflow adds an independent blank board beside the video");
  require(boards.saveDocument(), "Save mixed image and video carousel");
  CarouselDocumentController reopened;
  require(reopened.loadDocument(document_id) && reopened.boardSequence(0).value("sequencePath") == sequence.value("sequencePath") && reopened.boardSequence(1).isEmpty(),
          "Each board retains its own sequence and independent state after reopening");
  require(timeline.openCarouselBoardSequence(&reopened, 0), "Reopen the saved board sequence for further trimming and positioning");
  bool found_video = false;
  const auto clips = timeline.clips();
  for (int index = 0; index < clips.size(); ++index) if (clips[index].toMap().value("assetId") == "test-video") {
    timeline.selectClip(index); const auto clip = timeline.currentClip();
    require(qAbs(clip.value("trimStartSeconds").toDouble() - 1.0) < 0.01 && qAbs(clip.value("trimEndSeconds").toDouble() - 2.5) < 0.01,
            "The editable saved sequence retains the exact video trims");
    found_video = true;
  }
  require(found_video && timeline.cancelCarouselBoardSequence(), "Return without changing the saved sequence");
  const auto output = runtime.filePath("mixed-carousel-export");
  require(QDir().mkpath(output), "Create the mixed carousel export folder");
  reopened.setExportImageFormat("png");
  reopened.setExportBoardScope("all");
  reopened.setExportProofSheetEnabled(false); reopened.setExportManifestEnabled(true);
  require(reopened.exportCarouselToDirectory(output), "Export mixed carousel");
  require(wait([&] { return !reopened.exportBusy(); }), "Finish mixed carousel export");
  require(QDir(output).entryList({"*.mp4"}, QDir::Files).size() == 1 && QDir(output).entryList({"*.png"}, QDir::Files).size() == 1,
          "Carousel export includes the moving video board as MP4 and the new blank board as an image");
  std::cout << "Board sequences: trim, spatial placement, audio, real motion, parent restoration, Undo/Redo, reopen, plus-add and mixed export passed\n";
}
