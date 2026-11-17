// Diagnostic use only: render a real, previously MCP-authored project through
// the shared controller while keeping test data and font registration isolated.
// This is renderer proof, not a substitute for visible native UI acceptance.
namespace {

struct AuthoredProbeRange final {
  double start_seconds = 0.0;
  double end_seconds = 0.0;
  qint64 start_frame = 0;
  qint64 end_frame = 0;
};

bool parse_authored_probe_range(const QStringList& arguments,
                                const TimelineDocumentController& controller,
                                AuthoredProbeRange* range,
                                QString* error_message) {
  const auto start_flag = QStringLiteral("--probe-range-start-seconds");
  const auto end_flag = QStringLiteral("--probe-range-end-seconds");
  const auto has_start = arguments.contains(start_flag);
  const auto has_end = arguments.contains(end_flag);
  if (!has_start && !has_end) return false;
  const auto reject = [error_message](const QString& message) {
    if (error_message) *error_message = message;
    return false;
  };
  if (!has_start || !has_end || range == nullptr) {
    return reject(QStringLiteral("Provide both frame-aligned range start and end values."));
  }
  const auto value_for = [&arguments](const QString& flag, bool* ok) {
    const auto index = arguments.indexOf(flag);
    if (index < 0 || index + 1 >= arguments.size()) {
      *ok = false;
      return 0.0;
    }
    return arguments.at(index + 1).toDouble(ok);
  };
  bool start_ok = false;
  bool end_ok = false;
  const auto start = value_for(start_flag, &start_ok);
  const auto end = value_for(end_flag, &end_ok);
  const auto fps = controller.framesPerSecond();
  if (!start_ok || !end_ok || !std::isfinite(start) || !std::isfinite(end) ||
      !std::isfinite(fps) || fps <= 0.0 || start < 0.0 || end <= start ||
      end > controller.totalDurationSecondsValue() + 1.0e-9 ||
      end - start > 10.0 + 1.0e-9) {
    return reject(QStringLiteral("The requested diagnostic range must be positive, within the timeline, and at most ten seconds."));
  }
  const auto start_frame_value = start * fps;
  const auto end_frame_value = end * fps;
  const auto rounded_start_frame = std::round(start_frame_value);
  const auto rounded_end_frame = std::round(end_frame_value);
  constexpr double kFrameAlignmentTolerance = 1.0e-6;
  if (std::abs(start_frame_value - rounded_start_frame) > kFrameAlignmentTolerance ||
      std::abs(end_frame_value - rounded_end_frame) > kFrameAlignmentTolerance ||
      rounded_start_frame < 0.0 || rounded_end_frame <= rounded_start_frame ||
      rounded_start_frame >= static_cast<double>(std::numeric_limits<qint64>::max()) ||
      rounded_end_frame >= static_cast<double>(std::numeric_limits<qint64>::max()) ||
      rounded_end_frame - rounded_start_frame > 600.0) {
    return reject(QStringLiteral("Diagnostic range endpoints must align to the sequence frame clock and contain at most 600 frames."));
  }
  range->start_seconds = start;
  range->end_seconds = end;
  range->start_frame = static_cast<qint64>(rounded_start_frame);
  range->end_frame = static_cast<qint64>(rounded_end_frame);
  return true;
}

bool render_authored_probe_range(TimelineDocumentController& controller,
                                 const QString& workspace_path,
                                 const QString& file_stem,
                                 const AuthoredProbeRange& range,
                                 QString* output_path,
                                 QString* error_message,
                                 const QString& output_kind =
                                     QStringLiteral("bounded-h264-video"),
                                 const QString& output_suffix = QStringLiteral("mp4")) {
  using photo_editor::desktop::timeline_motion::NativeGeneratorFrameSequenceLease;
  const auto suffix = output_suffix.startsWith(QLatin1Char('.'))
                          ? output_suffix.mid(1) : output_suffix;
  if (suffix != QStringLiteral("mp4") && suffix != QStringLiteral("mov")) {
    if (error_message) *error_message = QStringLiteral("Unsupported diagnostic output suffix.");
    return false;
  }
  const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
  const auto output = QDir(workspace_path).filePath(file_stem + QLatin1Char('-') + token +
                                                     QLatin1Char('.') + suffix);
  const auto graph = QDir(workspace_path).filePath(file_stem + QLatin1Char('-') + token +
                                                    QStringLiteral(".ffgraph"));
  if (QFileInfo::exists(output) || QFileInfo::exists(graph)) {
    if (error_message) *error_message = QStringLiteral("The unique diagnostic output path already exists.");
    return false;
  }
  NativeGeneratorFrameSequenceLease generator_lease;
  const auto arguments =
      photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
          controller, output, output_kind,
          range.start_seconds, range.end_seconds, error_message, graph,
          &generator_lease);
  if (arguments.isEmpty() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
          controller, arguments, error_message)) {
    QFile::remove(graph);
    QFile::remove(output);
    return false;
  }
  QFile::remove(graph);
  if (!QFileInfo(output).isFile() || QFileInfo(output).size() <= 0) {
    if (error_message) *error_message = QStringLiteral("The bounded diagnostic renderer produced no output.");
    return false;
  }
  if (output_path) *output_path = output;
  return true;
}

QByteArray decode_authored_rgba_frame(
    const QString& ffmpeg,
    const QString& path,
    qint64 frame_index,
    const QSize& frame_size) {
  QProcess process;
  process.start(
      ffmpeg,
      {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-i"), path,
       QStringLiteral("-vf"),
       QStringLiteral("select=eq(n\\,%1)").arg(frame_index),
       QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba"),
       QStringLiteral("pipe:1")});
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  const auto bytes = process.readAllStandardOutput();
  const auto expected_size = static_cast<qint64>(frame_size.width()) *
                             frame_size.height() * 4;
  return bytes.size() == expected_size ? bytes : QByteArray{};
}

QColor authored_rgb_sample(const QByteArray& rgb,
                           const QSize& frame_size,
                           int x,
                           int y) {
  if (x < 0 || y < 0 || x >= frame_size.width() || y >= frame_size.height() ||
      rgb.size() != frame_size.width() * frame_size.height() * 3) {
    return {};
  }
  const auto offset = (y * frame_size.width() + x) * 3;
  return QColor(
      static_cast<int>(static_cast<unsigned char>(rgb.at(offset))),
      static_cast<int>(static_cast<unsigned char>(rgb.at(offset + 1))),
      static_cast<int>(static_cast<unsigned char>(rgb.at(offset + 2))));
}

QColor authored_rgba_sample(const QByteArray& rgba,
                            const QSize& frame_size,
                            int x,
                            int y) {
  if (x < 0 || y < 0 || x >= frame_size.width() || y >= frame_size.height() ||
      rgba.size() != frame_size.width() * frame_size.height() * 4) {
    return {};
  }
  const auto offset = (y * frame_size.width() + x) * 4;
  return QColor(
      static_cast<int>(static_cast<unsigned char>(rgba.at(offset))),
      static_cast<int>(static_cast<unsigned char>(rgba.at(offset + 1))),
      static_cast<int>(static_cast<unsigned char>(rgba.at(offset + 2))),
      static_cast<int>(static_cast<unsigned char>(rgba.at(offset + 3))));
}

bool populate_opaque_color_chart(TimelineDocumentController* controller,
                                 const QString& name,
                                 const QSize& frame_size,
                                 const QString& codec,
                                 const QString& container) {
  if (controller == nullptr) return false;
  controller->setEditAccessPolicy([] { return true; });
  controller->createTimelineWithSettings({
      {QStringLiteral("timelineName"), name},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("exportCodec"), codec},
      {QStringLiteral("exportContainer"), container},
  });
  const auto add_graphic = [controller](const QString& label,
                                        const QString& color,
                                        double center_x,
                                        double width) {
    const auto track = controller->createTimelineTrackForAutomation(
        QStringLiteral("graphics"), label, {});
    const auto track_id = track.value(QStringLiteral("trackId")).toString();
    const auto graphic = controller->createTimelineGraphicForAutomation(
        track_id, QStringLiteral("rectangle"), 0.0, 0.2,
        QVariantMap{
            {QStringLiteral("positionX"), center_x},
            {QStringLiteral("positionY"), 0.5},
            {QStringLiteral("shapeWidth"), width},
            {QStringLiteral("shapeHeight"), 1.0},
            {QStringLiteral("shapeStrokeWidth"), 0.0},
            {QStringLiteral("backgroundPadding"), 0.0},
            {QStringLiteral("backgroundColor"), color},
        });
    return track.value(QStringLiteral("ok")).toBool() &&
           graphic.value(QStringLiteral("ok")).toBool();
  };
  return add_graphic(QStringLiteral("Color chart base"),
                     QStringLiteral("#ff204060"), 0.5, 1.0) &&
         add_graphic(QStringLiteral("Black swatch"),
                     QStringLiteral("#ff000000"), 0.125, 0.25) &&
         add_graphic(QStringLiteral("Gray swatch"),
                     QStringLiteral("#ff808080"), 0.375, 0.25) &&
         add_graphic(QStringLiteral("Red swatch"),
                     QStringLiteral("#ffff0000"), 0.625, 0.25);
}

bool render_authored_color_chart(TimelineDocumentController& controller,
                                 const QString& workspace_path,
                                 const QString& stem,
                                 const QString& output_kind,
                                 const QString& suffix,
                                 QString* output_path,
                                 QString* error_message) {
  constexpr double kDuration = 0.2;
  constexpr qint64 kFrames = 6;
  const AuthoredProbeRange range{0.0, kDuration, 0, kFrames};
  return render_authored_probe_range(controller, workspace_path, stem, range,
                                     output_path, error_message, output_kind,
                                     suffix);
}

bool run_authored_color_range_conversion_regression(
    const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << "authored color-range regression: " << message << '\n';
    return condition;
  };
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (!check(!ffmpeg.isEmpty() && !ffprobe.isEmpty(),
             "FFmpeg and ffprobe must be available")) {
    return false;
  }
  const QSize frame_size(128, 72);
  TimelineDocumentController h264_controller;
  if (!check(populate_opaque_color_chart(
                 &h264_controller, QStringLiteral("Opaque H.264 color-range chart"),
                 frame_size, QStringLiteral("h264"), QStringLiteral("mp4")),
             "an H.264 swatch chart should be authored")) {
    return false;
  }
  QString bounded_path;
  QString bounded_error;
  QString master_path;
  QString master_error;
  QString cache_path;
  QString cache_error;
  if (!render_authored_color_chart(
          h264_controller, workspace_path, QStringLiteral("color-bounded"),
          QStringLiteral("bounded-h264-video"), QStringLiteral("mp4"),
          &bounded_path, &bounded_error) ||
      !render_authored_color_chart(
          h264_controller, workspace_path, QStringLiteral("color-master-h264"),
          QStringLiteral("master"), QStringLiteral("mp4"),
          &master_path, &master_error) ||
      !render_authored_color_chart(
          h264_controller, workspace_path, QStringLiteral("color-render-cache"),
          QStringLiteral("render-cache"), QStringLiteral("mp4"),
          &cache_path, &cache_error)) {
    std::cerr << "bounded=" << bounded_error.toStdString()
              << " master=" << master_error.toStdString()
              << " cache=" << cache_error.toStdString() << '\n';
    return check(false, "bounded, master, and render-cache H.264 graphs should render the chart");
  }
  const auto check_opaque_chart = [&](const QString& path, int tolerance,
                                      const char* path_name) {
    const auto frame_count = count_video_frames(ffprobe, path);
    const auto frame = decode_video_frame_rgb24(ffmpeg, path, 0, frame_size);
    const auto black = authored_rgb_sample(frame, frame_size, 16, 36);
    const auto gray = authored_rgb_sample(frame, frame_size, 48, 36);
    const auto red = authored_rgb_sample(frame, frame_size, 80, 36);
    const auto base = authored_rgb_sample(frame, frame_size, 112, 36);
    const auto near = [tolerance](int actual, int expected) {
      return qAbs(actual - expected) <= tolerance;
    };
    const auto valid = frame_count == 6 && black.isValid() && gray.isValid() &&
                       red.isValid() && base.isValid() &&
                       near(black.red(), 0) && near(black.green(), 0) &&
                       near(black.blue(), 0) && near(gray.red(), 128) &&
                       near(gray.green(), 128) && near(gray.blue(), 128) &&
                       near(red.red(), 255) && near(red.green(), 0) &&
                       near(red.blue(), 0) && near(base.red(), 32) &&
                       near(base.green(), 64) && near(base.blue(), 96);
    if (!valid) {
      std::cerr << path_name << " frames=" << frame_count
                << " black=" << black.red() << ',' << black.green() << ',' << black.blue()
                << " gray=" << gray.red() << ',' << gray.green() << ',' << gray.blue()
                << " red=" << red.red() << ',' << red.green() << ',' << red.blue()
                << " base=" << base.red() << ',' << base.green() << ',' << base.blue()
                << '\n';
    }
    return valid;
  };
  if (!check(check_opaque_chart(bounded_path, 10, "bounded H.264"),
             "bounded H.264 must preserve black, gray, red, and dark-blue RGB levels")) {
    return false;
  }
  if (!check(check_opaque_chart(master_path, 10, "master H.264"),
             "master H.264 must preserve black, gray, red, and dark-blue RGB levels")) {
    return false;
  }
  if (!check(check_opaque_chart(cache_path, 10, "render-cache H.264"),
             "render-cache H.264 must preserve black, gray, red, and dark-blue RGB levels")) {
    return false;
  }

  TimelineDocumentController prores_controller;
  if (!check(populate_opaque_color_chart(
                 &prores_controller, QStringLiteral("Opaque ProRes color-range chart"),
                 frame_size, QStringLiteral("prores"), QStringLiteral("mov")),
             "an opaque ProRes swatch chart should be authored")) {
    return false;
  }
  QString prores_path;
  QString prores_error;
  if (!render_authored_color_chart(
          prores_controller, workspace_path, QStringLiteral("color-master-prores"),
          QStringLiteral("master"), QStringLiteral("mov"),
          &prores_path, &prores_error)) {
    std::cerr << "ProRes=" << prores_error.toStdString() << '\n';
    return check(false, "opaque ProRes should render the same swatch chart");
  }
  if (!check(check_opaque_chart(prores_path, 6, "opaque ProRes"),
             "opaque ProRes must preserve black, gray, red, and dark-blue RGB levels")) {
    return false;
  }

  TimelineDocumentController alpha_controller;
  alpha_controller.setEditAccessPolicy([] { return true; });
  alpha_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Alpha ProRes conversion control")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("exportCodec"), QStringLiteral("prores")},
      {QStringLiteral("exportContainer"), QStringLiteral("mov")},
  });
  const auto alpha_track = alpha_controller.createTimelineTrackForAutomation(
      QStringLiteral("graphics"), QStringLiteral("Partial alpha"), {});
  const auto alpha_graphic = alpha_controller.createTimelineGraphicForAutomation(
      alpha_track.value(QStringLiteral("trackId")).toString(),
      QStringLiteral("rectangle"), 0.0, 0.2,
      QVariantMap{
          {QStringLiteral("positionX"), 0.25},
          {QStringLiteral("positionY"), 0.5},
          {QStringLiteral("shapeWidth"), 0.5},
          {QStringLiteral("shapeHeight"), 1.0},
          {QStringLiteral("shapeStrokeWidth"), 0.0},
          {QStringLiteral("backgroundPadding"), 0.0},
          {QStringLiteral("backgroundColor"), QStringLiteral("#80ff0000")},
      });
  if (!alpha_track.value(QStringLiteral("ok")).toBool() ||
      !alpha_graphic.value(QStringLiteral("ok")).toBool() ||
      !alpha_controller.setDeliverySetting(QStringLiteral("alpha"), true)) {
    return check(false, "the alpha ProRes control should author a partial-alpha swatch");
  }
  QString alpha_path;
  QString alpha_error;
  if (!render_authored_color_chart(
          alpha_controller, workspace_path, QStringLiteral("color-alpha-prores"),
          QStringLiteral("master"), QStringLiteral("mov"),
          &alpha_path, &alpha_error)) {
    std::cerr << "Alpha ProRes=" << alpha_error.toStdString() << '\n';
    return check(false, "alpha ProRes control should render");
  }
  const auto alpha_format = process_stdout(
      ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-select_streams"), QStringLiteral("v:0"),
                QStringLiteral("-show_entries"), QStringLiteral("stream=pix_fmt"),
                QStringLiteral("-of"), QStringLiteral("default=nw=1:nk=1"), alpha_path});
  const auto alpha_frame = decode_authored_rgba_frame(ffmpeg, alpha_path, 0, frame_size);
  const auto partial = authored_rgba_sample(alpha_frame, frame_size, 16, 36);
  const auto transparent = authored_rgba_sample(alpha_frame, frame_size, 96, 36);
  if (!check(alpha_format.startsWith(QStringLiteral("yuva444")) &&
                 count_video_frames(ffprobe, alpha_path) == 6 &&
                 partial.isValid() && transparent.isValid() &&
                 qAbs(partial.alpha() - 128) <= 8 && transparent.alpha() <= 8,
             "the alpha ProRes path must retain partial coverage and transparent pixels")) {
    std::cerr << "alpha format=" << alpha_format.toStdString()
              << " partial RGBA=" << partial.red() << ',' << partial.green() << ','
              << partial.blue() << ',' << partial.alpha()
              << " transparent RGBA=" << transparent.red() << ','
              << transparent.green() << ',' << transparent.blue() << ','
              << transparent.alpha() << '\n';
    return false;
  }
  return true;
}

}  // namespace

bool run_authored_project_render_probe(const QStringList& arguments,
                                      const QString& workspace_path);

bool run_authored_project_range_probe_fixture_test(const QString& workspace_path) {
  const auto check = [](bool condition, const char* message) {
    if (!condition) std::cerr << "authored range probe fixture: " << message << '\n';
    return condition;
  };
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (!check(!ffprobe.isEmpty() && !ffmpeg.isEmpty(),
             "FFmpeg and ffprobe must be available")) {
    return false;
  }
  if (!run_authored_color_range_conversion_regression(workspace_path)) {
    return false;
  }

  constexpr double kFps = 30.0;
  constexpr double kClipStart = 131.8;
  constexpr double kClipDuration = 0.4;
  constexpr double kRangeStart = 132.0;
  constexpr double kRangeEnd = 132.2;
  constexpr qint64 kExpectedFrames = 6;
  const QSize frame_size(128, 72);
  TimelineDocumentController controller;
  controller.setEditAccessPolicy([] { return true; });
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Authored range clock oracle")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), kFps},
  });
  const auto track = controller.createTimelineTrackForAutomation(
      QStringLiteral("graphics"), QStringLiteral("Range oracle"), {});
  const auto track_id = track.value(QStringLiteral("trackId")).toString();
  const auto created = controller.createTimelineGraphicForAutomation(
      track_id, QStringLiteral("rectangle"), kClipStart, kClipDuration,
      QVariantMap{
          {QStringLiteral("positionX"), 0.5},
          {QStringLiteral("positionY"), 0.5},
          {QStringLiteral("shapeWidth"), 1.0},
          {QStringLiteral("shapeHeight"), 1.0},
          {QStringLiteral("shapeStrokeWidth"), 0.0},
          {QStringLiteral("backgroundPadding"), 0.0},
          {QStringLiteral("backgroundColor"), QStringLiteral("#ffff0000")},
      });
  const auto clip_id = created.value(QStringLiteral("createdClipId")).toString();
  const auto animation = controller.setTimelineClipAnimationForAutomation(
      clip_id,
      QVariantList{QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("keyframes"), QVariantList{
              QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                          {QStringLiteral("value"), 0.25},
                          {QStringLiteral("interpolation"), QStringLiteral("linear")}},
              QVariantMap{{QStringLiteral("localSeconds"), 0.2},
                          {QStringLiteral("value"), 1.0},
                          {QStringLiteral("interpolation"), QStringLiteral("linear")}},
              QVariantMap{{QStringLiteral("localSeconds"), 0.4},
                          {QStringLiteral("value"), 0.25},
                          {QStringLiteral("interpolation"), QStringLiteral("linear")}},
          }},
      }});
  if (!check(track.value(QStringLiteral("ok")).toBool() &&
                 created.value(QStringLiteral("ok")).toBool() &&
                 animation.value(QStringLiteral("ok")).toBool() &&
                 qAbs(controller.totalDurationSecondsValue() - kRangeEnd) < 1.0e-6,
             "a generated clip with local keyframes should span the global range")) {
    return false;
  }
  const auto source_document_hash = controller.deterministicDocumentHash();
  const auto source_path = QDir(workspace_path).filePath(
      QStringLiteral("generated-range-authored-project.json"));
  const auto saved_copy = controller.saveTimelineCopyForAutomation(source_path);
  if (!check(saved_copy.value(QStringLiteral("ok")).toBool() &&
                 QFileInfo(source_path).isFile(),
             "the generated source project should save to the isolated test workspace")) {
    return false;
  }
  QFile source_project(source_path);
  if (!source_project.open(QIODevice::ReadOnly)) {
    return check(false, "the generated source project should be readable");
  }
  const auto source_project_bytes = source_project.readAll();
  source_project.close();
  const auto source_project_sha = QCryptographicHash::hash(
      source_project_bytes, QCryptographicHash::Sha256);
  const auto autosave_path = source_path + QStringLiteral(".autosave");
  const auto font_root = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
                             .filePath(QStringLiteral("apps/desktop/assets/fonts"));
  const auto run_probe = [&](const QString& output_path) {
    return run_authored_project_render_probe(
        {QStringLiteral("--authored-project"), source_path,
         QStringLiteral("--probe-output"), output_path,
         QStringLiteral("--probe-font-root"), font_root,
         QStringLiteral("--probe-range-start-seconds"), QStringLiteral("132.0"),
         QStringLiteral("--probe-range-end-seconds"), QStringLiteral("132.2")},
        workspace_path);
  };
  const auto output_without_autosave = QDir(workspace_path).filePath(
      QStringLiteral("generated-range-no-autosave.mp4"));
  if (QFileInfo::exists(autosave_path) || !run_probe(output_without_autosave) ||
      QFileInfo::exists(autosave_path)) {
    return check(false, "the private-copy probe should not create an adjacent autosave for the source project");
  }
  QFile source_after(source_path);
  if (!source_after.open(QIODevice::ReadOnly)) {
    return check(false, "the original source project should remain readable after the probe");
  }
  const auto source_after_bytes = source_after.readAll();
  source_after.close();
  if (!check(source_after_bytes == source_project_bytes &&
                 QCryptographicHash::hash(source_after_bytes, QCryptographicHash::Sha256) ==
                     source_project_sha &&
                 controller.deterministicDocumentHash() == source_document_hash,
             "the authored input and generated keyframes should remain unchanged")) {
    return false;
  }
  const auto actual_frames = count_video_frames(ffprobe, output_without_autosave);
  const auto first_frame = decode_video_frame_rgb24(
      ffmpeg, output_without_autosave, 0, frame_size);
  const auto last_frame = decode_video_frame_rgb24(
      ffmpeg, output_without_autosave, kExpectedFrames - 1, frame_size);
  const auto center_pixel = [frame_size](const QByteArray& rgb) {
    if (rgb.size() != frame_size.width() * frame_size.height() * 3) return QColor{};
    const auto center_offset =
        ((frame_size.height() / 2) * frame_size.width() + frame_size.width() / 2) * 3;
    return QColor(
        static_cast<int>(static_cast<unsigned char>(rgb.at(center_offset))),
        static_cast<int>(static_cast<unsigned char>(rgb.at(center_offset + 1))),
        static_cast<int>(static_cast<unsigned char>(rgb.at(center_offset + 2))));
  };
  const auto first_pixel = center_pixel(first_frame);
  const auto last_pixel = center_pixel(last_frame);
  constexpr int kYuvH264CodeTolerance = 12;
  constexpr int kExpectedFirstRed = 255;  // local t=0.2s, opacity 1.0
  constexpr int kExpectedLastRed = 96;    // local t=0.3667s, opacity 0.375
  const auto close_to_expected = [=](int actual, int expected) {
    return qAbs(actual - expected) <= kYuvH264CodeTolerance;
  };
  if (!check(actual_frames == kExpectedFrames && first_pixel.isValid() &&
                 last_pixel.isValid() &&
                 close_to_expected(first_pixel.red(), kExpectedFirstRed) &&
                 close_to_expected(last_pixel.red(), kExpectedLastRed) &&
                 first_pixel.green() <= kYuvH264CodeTolerance &&
                 first_pixel.blue() <= kYuvH264CodeTolerance &&
                 last_pixel.green() <= kYuvH264CodeTolerance &&
                 last_pixel.blue() <= kYuvH264CodeTolerance &&
                 controller.deterministicDocumentHash() == source_document_hash,
             "six bounded frames should match the exact global-clock opacity samples at local t=0.2s (1.0) and t=0.3667s (0.375), with only 12-code H.264/YUV tolerance")) {
    std::cerr << "frames=" << actual_frames << " expected=" << kExpectedFrames
              << " firstRGB=" << first_pixel.red() << ',' << first_pixel.green() << ','
              << first_pixel.blue() << " lastRGB=" << last_pixel.red() << ','
              << last_pixel.green() << ',' << last_pixel.blue() << '\n';
    return false;
  }
  return true;
}

bool run_authored_project_render_probe(const QStringList& arguments,
                                      const QString& workspace_path) {
  const auto argument = [&arguments](const QString& flag) {
    const auto index = arguments.indexOf(flag);
    return index >= 0 && index + 1 < arguments.size()
               ? arguments.at(index + 1) : QString{};
  };
  const auto source = QFileInfo(argument(QStringLiteral("--authored-project")));
  const auto output = QFileInfo(argument(QStringLiteral("--probe-output")));
  const auto font_root = QFileInfo(argument(QStringLiteral("--probe-font-root")));
  if (!source.isFile() || !source.isReadable() || source.isSymLink() ||
      source.suffix() != QStringLiteral("json") || source.size() > 256 * 1024 * 1024 ||
      output.fileName().isEmpty() || output.exists() || output.isSymLink() ||
      output.suffix() != QStringLiteral("mp4") ||
      !QFileInfo(output.absolutePath()).isDir() || !font_root.isDir()) {
    std::cerr << "Provide an existing native JSON project, a new MP4 output, and the bundled font directory.\n";
    return false;
  }
  QFile input(source.canonicalFilePath());
  if (!input.open(QIODevice::ReadOnly)) return false;
  const auto source_before = input.readAll();
  input.close();
  const auto source_sha_before = QCryptographicHash::hash(
      source_before, QCryptographicHash::Sha256);
  const auto original_autosave_path =
      source.canonicalFilePath() + QStringLiteral(".autosave");
  const QFileInfo original_autosave_info(original_autosave_path);
  const auto original_autosave_present =
      original_autosave_info.exists() || original_autosave_info.isSymLink();
  if (original_autosave_present &&
      (original_autosave_info.isSymLink() || !original_autosave_info.isFile() ||
       !original_autosave_info.isReadable() ||
       original_autosave_info.size() > 256 * 1024 * 1024)) {
    std::cerr << "The original project's adjacent autosave cannot be safely fingerprinted.\n";
    return false;
  }
  QByteArray original_autosave_before;
  if (original_autosave_present) {
    QFile autosave(original_autosave_path);
    if (!autosave.open(QIODevice::ReadOnly)) {
      std::cerr << "The original project's adjacent autosave cannot be read.\n";
      return false;
    }
    original_autosave_before = autosave.readAll();
  }
  const auto original_autosave_sha_before = QCryptographicHash::hash(
      original_autosave_before, QCryptographicHash::Sha256);
  const auto workspace_info = QFileInfo(workspace_path);
  const auto canonical_workspace = workspace_info.canonicalFilePath();
  if (!workspace_info.isDir() || canonical_workspace.isEmpty()) {
    std::cerr << "The private probe workspace is unavailable.\n";
    return false;
  }
  const auto private_source_path = QDir(canonical_workspace).filePath(
      QStringLiteral("authored-source-%1.json")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  if (QFileInfo::exists(private_source_path)) {
    std::cerr << "The unique private project copy path already exists.\n";
    return false;
  }
  QSaveFile private_source_file(private_source_path);
  if (!private_source_file.open(QIODevice::WriteOnly) ||
      private_source_file.write(source_before) != source_before.size() ||
      !private_source_file.commit()) {
    std::cerr << "Unable to write the byte-identical private project copy.\n";
    return false;
  }
  const QFileInfo private_source_info(private_source_path);
  const auto canonical_private_source = private_source_info.canonicalFilePath();
  const auto workspace_relative_source =
      QDir(canonical_workspace).relativeFilePath(canonical_private_source);
  if (!private_source_info.isFile() || private_source_info.isSymLink() ||
      canonical_private_source.isEmpty() || workspace_relative_source == QStringLiteral("..") ||
      workspace_relative_source.startsWith(QStringLiteral("../")) ||
      workspace_relative_source.startsWith(QStringLiteral("..\\"))) {
    std::cerr << "The private project copy escaped its owned workspace.\n";
    return false;
  }
  QFile verify_private_source(canonical_private_source);
  if (!verify_private_source.open(QIODevice::ReadOnly)) {
    std::cerr << "Unable to read back the private project copy.\n";
    return false;
  }
  const auto private_source_bytes = verify_private_source.readAll();
  verify_private_source.close();
  const auto private_source_sha = QCryptographicHash::hash(
      private_source_bytes, QCryptographicHash::Sha256);
  if (private_source_bytes != source_before || private_source_sha != source_sha_before) {
    std::cerr << "The private project copy did not preserve the original bytes.\n";
    return false;
  }
  auto registered_fonts = 0;
  QDirIterator fonts(font_root.canonicalFilePath(),
                     {QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
                     QDir::Files, QDirIterator::Subdirectories);
  while (fonts.hasNext()) {
    if (QFontDatabase::addApplicationFont(fonts.next()) < 0) {
      std::cerr << "Unable to register one of the exact bundled fonts.\n";
      return false;
    }
    ++registered_fonts;
  }
  if (registered_fonts == 0) return false;
  TimelineDocumentController controller;
  if (!controller.loadTimeline(canonical_private_source) || controller.clipCount() == 0) {
    std::cerr << "Unable to load the authored project: " << controller.status().toStdString() << '\n';
    return false;
  }
  const auto original_clip_count = controller.clipCount();
  const auto prefix_supplied = arguments.contains(QStringLiteral("--probe-prefix-seconds"));
  const auto range_start_supplied = arguments.contains(QStringLiteral("--probe-range-start-seconds"));
  const auto range_end_supplied = arguments.contains(QStringLiteral("--probe-range-end-seconds"));
  if ((range_start_supplied || range_end_supplied) && prefix_supplied) {
    std::cerr << "Use either a prefix render or a bounded range render, not both.\n";
    return false;
  }
  AuthoredProbeRange range;
  QString range_error;
  const auto range_requested =
      parse_authored_probe_range(arguments, controller, &range, &range_error);
  if ((range_start_supplied || range_end_supplied) && !range_requested) {
    std::cerr << range_error.toStdString() << '\n';
    return false;
  }
  if (range_requested &&
      QStandardPaths::findExecutable(QStringLiteral("ffprobe")).isEmpty()) {
    std::cerr << "ffprobe is required to verify the bounded diagnostic frame count.\n";
    return false;
  }
  const auto prefix_text = argument(QStringLiteral("--probe-prefix-seconds"));
  if (prefix_supplied) {
    bool parsed = false;
    const auto prefix = prefix_text.toDouble(&parsed);
    if (!parsed || !std::isfinite(prefix) || prefix < 1.0 / controller.framesPerSecond() ||
        prefix > controller.totalDurationSecondsValue()) {
      std::cerr << "Choose a positive prefix no longer than the authored project.\n";
      return false;
    }
    // Diagnostic sampling only: trim a private in-memory controller through
    // native edit methods. Never alter or reserialize the MCP-authored source.
    const auto clips = controller.inspectAgentDocument().value(QStringLiteral("clips")).toList();
    QStringList later_ids;
    for (const auto& value : clips) {
      const auto clip = value.toMap();
      if (clip.value(QStringLiteral("startSeconds")).toDouble() >= prefix)
        later_ids.append(clip.value(QStringLiteral("clipId")).toString());
    }
    if (!later_ids.isEmpty()) {
      controller.selectClipsByIds(later_ids);
      controller.removeSelectedClips();
    }
    for (const auto& value : clips) {
      const auto clip = value.toMap();
      const auto start = clip.value(QStringLiteral("startSeconds")).toDouble();
      const auto duration = clip.value(QStringLiteral("trimmedDurationSeconds")).toDouble();
      if (start < prefix && start + duration > prefix) {
        const auto edited = controller.setTimelineClipTimingForAutomation(
            clip.value(QStringLiteral("clipId")).toString(),
            clip.value(QStringLiteral("trackId")).toString(), start, prefix - start);
        if (!edited.value(QStringLiteral("ok")).toBool()) {
          std::cerr << "Unable to trim the private diagnostic prefix.\n";
          return false;
        }
      }
    }
    if (qAbs(controller.totalDurationSecondsValue() - prefix) > 0.000001) {
      std::cerr << "The diagnostic prefix did not retain the requested duration.\n";
      return false;
    }
  }
  const auto temporary_output = QDir(workspace_path).filePath(QStringLiteral("authored-render.mp4"));
  QElapsedTimer elapsed;
  elapsed.start();
  QString range_output;
  const auto rendered = range_requested
      ? render_authored_probe_range(controller, workspace_path,
                                    QStringLiteral("authored-range"), range,
                                    &range_output, &range_error)
      : controller.renderCurrentTimeline(temporary_output);
  if (!rendered) {
    std::cerr << "Native authored-project rendering failed: " << controller.status().toStdString() << '\n';
    if (range_requested) std::cerr << range_error.toStdString() << '\n';
    return false;
  }
  const auto milliseconds = elapsed.elapsed();
  QFile source_after_file(source.canonicalFilePath());
  if (!source_after_file.open(QIODevice::ReadOnly)) {
    std::cerr << "The original authored project became unreadable during rendering.\n";
    return false;
  }
  const auto source_after = source_after_file.readAll();
  source_after_file.close();
  const QFileInfo autosave_after_info(original_autosave_path);
  QByteArray original_autosave_after;
  if (original_autosave_present) {
    if (autosave_after_info.isSymLink() || !autosave_after_info.isFile() ||
        !autosave_after_info.isReadable()) {
      std::cerr << "The original project's adjacent autosave changed type during rendering.\n";
      return false;
    }
    QFile autosave_after_file(original_autosave_path);
    if (!autosave_after_file.open(QIODevice::ReadOnly)) {
      std::cerr << "The original project's adjacent autosave became unreadable during rendering.\n";
      return false;
    }
    original_autosave_after = autosave_after_file.readAll();
  }
  const auto original_autosave_sha_after = QCryptographicHash::hash(
      original_autosave_after, QCryptographicHash::Sha256);
  const auto source_sha_after = QCryptographicHash::hash(
      source_after, QCryptographicHash::Sha256);
  const auto autosave_preserved = original_autosave_present
      ? original_autosave_after == original_autosave_before &&
            original_autosave_sha_after == original_autosave_sha_before
      : !autosave_after_info.exists() && !autosave_after_info.isSymLink();
  if (source_after != source_before ||
      source_sha_after != source_sha_before ||
      !autosave_preserved) {
    std::cerr << "Rendering changed the original project bytes or its adjacent autosave state.\n";
    return false;
  }
  const auto completed_output = range_requested ? range_output : temporary_output;
  const auto actual_frame_count = range_requested
      ? count_video_frames(QStandardPaths::findExecutable(QStringLiteral("ffprobe")),
                           completed_output)
      : -1;
  if ((range_requested && actual_frame_count != range.end_frame - range.start_frame) ||
      QFileInfo(completed_output).size() <= 0 ||
      !QFile::rename(completed_output, output.absoluteFilePath())) {
    std::cerr << "Unable to publish the new diagnostic render without replacing an existing file.\n";
    if (range_requested) {
      std::cerr << "Expected range frames=" << (range.end_frame - range.start_frame)
                << " actual=" << actual_frame_count << '\n';
    }
    return false;
  }
  // Persist backend evidence before the isolated temporary cache disappears.
  // These are the surviving source caches, not a count of every rendered frame.
  QVariantList source_cache_backends;
  QDirIterator manifests(qEnvironmentVariable("PHOTO_EDITOR_CACHE_DIR"),
                         {QStringLiteral("manifest.json")}, QDir::Files,
                         QDirIterator::Subdirectories);
  while (manifests.hasNext()) {
    QFile manifest_file(manifests.next());
    if (!manifest_file.open(QIODevice::ReadOnly)) continue;
    const auto manifest = QJsonDocument::fromJson(manifest_file.readAll()).object();
    if (!manifest.contains(QStringLiteral("backend")) ||
        !manifest.contains(QStringLiteral("generatorKind"))) continue;
    QVariantMap evidence;
    for (const auto& key : {QStringLiteral("generatorKind"), QStringLiteral("backend"),
                            QStringLiteral("metalFrameCount"), QStringLiteral("cpuFrameCount"),
                            QStringLiteral("fallbackReason"), QStringLiteral("width"),
                            QStringLiteral("height"), QStringLiteral("frameCount")}) {
      evidence.insert(key, manifest.value(key).toVariant());
    }
    source_cache_backends.push_back(evidence);
  }
  const QVariantMap receipt{
      {QStringLiteral("source"), source.canonicalFilePath()},
      {QStringLiteral("sourceSha256"), QString::fromLatin1(
          QCryptographicHash::hash(source_before, QCryptographicHash::Sha256).toHex())},
      {QStringLiteral("sourceSha256After"), QString::fromLatin1(source_sha_after.toHex())},
      {QStringLiteral("privateInputCopy"), canonical_private_source},
      {QStringLiteral("privateInputCopySha256"), QString::fromLatin1(private_source_sha.toHex())},
      {QStringLiteral("originalAutosavePath"), original_autosave_path},
      {QStringLiteral("originalAutosavePresent"), original_autosave_present},
      {QStringLiteral("originalAutosavePreserved"), autosave_preserved},
      {QStringLiteral("originalAutosaveSha256Before"),
       original_autosave_present
           ? QString::fromLatin1(original_autosave_sha_before.toHex()) : QString{}},
      {QStringLiteral("originalAutosaveSha256After"),
       original_autosave_present
           ? QString::fromLatin1(original_autosave_sha_after.toHex()) : QString{}},
      {QStringLiteral("output"), output.absoluteFilePath()},
      {QStringLiteral("width"), controller.sequenceWidth()},
      {QStringLiteral("height"), controller.sequenceHeight()},
      {QStringLiteral("fps"), controller.framesPerSecond()},
      {QStringLiteral("clips"), controller.clipCount()},
      {QStringLiteral("sourceClips"), original_clip_count},
      {QStringLiteral("sampledPrefixSeconds"), prefix_text},
      {QStringLiteral("renderRangeStartSeconds"), range_requested ? range.start_seconds : -1.0},
      {QStringLiteral("renderRangeEndSeconds"), range_requested ? range.end_seconds : -1.0},
      {QStringLiteral("renderRangeStartFrame"), range_requested ? range.start_frame : -1},
      {QStringLiteral("renderRangeEndFrameExclusive"), range_requested ? range.end_frame : -1},
      {QStringLiteral("actualRangeFrameCount"), actual_frame_count},
      {QStringLiteral("registeredFontFiles"), registered_fonts},
      {QStringLiteral("elapsedMilliseconds"), milliseconds},
      {QStringLiteral("survivingSourceCacheBackends"), source_cache_backends},
      {QStringLiteral("qualification"), QStringLiteral("Shared native controller render; no GUI workflow proof.")},
  };
  std::cout << QJsonDocument::fromVariant(receipt).toJson(QJsonDocument::Indented).toStdString();
  return true;
}
