bool run_timeline_frame_shadow_render_workflow(const QString& workspace_path) {
  const auto fail = [](const char* message) {
    std::cerr << "timeline_frame_shadow_render_workflow: " << message << '\n';
    return false;
  };
  if (QStandardPaths::findExecutable(QStringLiteral("ffmpeg")).isEmpty()) {
    std::cerr << "timeline_frame_shadow_render_workflow: FFmpeg is unavailable\n";
    return false;
  }

  QImage source(64, 48, QImage::Format_RGBA8888);
  source.fill(QColor(255, 255, 255, 255));
  const auto source_path =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-source.png"));
  if (!source.save(source_path)) return fail("the shadow fixture source should save");

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Frame Shadow Pixel Proof")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("trackLayout"), QStringLiteral("creator_social")},
  });
  controller.addClipFromGallery(QStringLiteral("photo:frame-shadow"), source_path,
                                QStringLiteral("Shadow card"), 1.0, source_path);
  const auto initial = controller.clips().value(0).toMap();
  const auto clip_id = initial.value(QStringLiteral("clipId")).toString();
  const auto track_id = initial.value(QStringLiteral("trackId")).toString();
  if (clip_id.isEmpty() || track_id.isEmpty()) {
    return fail("the editable photo clip should be created");
  }
  const auto initial_shadow = initial.value(QStringLiteral("frameShadow")).toMap();
  if (initial_shadow.value(QStringLiteral("enabled")).toBool()) {
    return fail("new clips should keep shadows disabled by default");
  }

  const auto legacy_copy =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-legacy.json"));
  const auto legacy_save = controller.saveTimelineCopyForAutomation(legacy_copy);
  QFile legacy_file(legacy_copy);
  if (!legacy_save.value(QStringLiteral("ok")).toBool() ||
      !legacy_file.open(QIODevice::ReadOnly) ||
      legacy_file.readAll().contains("\"frameShadow\"")) {
    return fail("default shadows must not change legacy serialized timeline graphs");
  }

  const QVariantMap shadow{
      {QStringLiteral("enabled"), true},
      {QStringLiteral("color"), QStringLiteral("#99000000")},
      {QStringLiteral("offsetX"), -8.0},
      {QStringLiteral("offsetY"), -8.0},
      {QStringLiteral("blurRadius"), 20.0},
      {QStringLiteral("spread"), 2.0},
  };
  const auto frame = controller.setTimelineClipFrameForAutomation(
      clip_id, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
      QStringLiteral("stretch"), 0.5, 0.5, 0.08,
      0.0, 0.0, 0.0, 0.0, shadow);
  const auto persisted = controller.inspectTimelineClipById(clip_id)
                             .value(QStringLiteral("clip")).toMap()
                             .value(QStringLiteral("frameShadow")).toMap();
  if (!frame.value(QStringLiteral("ok")).toBool() ||
      persisted.value(QStringLiteral("enabled")).toBool() != true ||
      persisted.value(QStringLiteral("color")).toString() != QStringLiteral("#99000000") ||
      !nearly_equal(persisted.value(QStringLiteral("offsetX")).toDouble(), -8.0) ||
      !nearly_equal(persisted.value(QStringLiteral("offsetY")).toDouble(), -8.0) ||
      !nearly_equal(persisted.value(QStringLiteral("blurRadius")).toDouble(), 20.0) ||
      !nearly_equal(persisted.value(QStringLiteral("spread")).toDouble(), 2.0)) {
    std::cerr << "timeline_frame_shadow_render_workflow: the frame action should retain validated shadow parameters; result="
              << QJsonDocument::fromVariant(frame).toJson(QJsonDocument::Compact).constData()
              << '\n';
    return false;
  }

  const auto enabled_copy =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-enabled.json"));
  const auto enabled_save = controller.saveTimelineCopyForAutomation(enabled_copy);
  QFile enabled_file(enabled_copy);
  if (!enabled_save.value(QStringLiteral("ok")).toBool() ||
      !enabled_file.open(QIODevice::ReadOnly) ||
      !enabled_file.readAll().contains("\"frameShadow\"")) {
    return fail("an enabled shadow should be included in the saved native project");
  }
  TimelineDocumentController reopened;
  if (!reopened.loadTimeline(enabled_copy)) {
    return fail("the shadow project should reopen as editable native state");
  }
  const auto reopened_shadow = reopened.inspectTimelineClipById(clip_id)
                                   .value(QStringLiteral("clip")).toMap()
                                   .value(QStringLiteral("frameShadow")).toMap();
  if (reopened_shadow.value(QStringLiteral("color")).toString() != QStringLiteral("#99000000") ||
      !nearly_equal(reopened_shadow.value(QStringLiteral("offsetX")).toDouble(), -8.0) ||
      !nearly_equal(reopened_shadow.value(QStringLiteral("blurRadius")).toDouble(), 20.0) ||
      !nearly_equal(reopened_shadow.value(QStringLiteral("spread")).toDouble(), 2.0)) {
    return fail("shadow state should round-trip with the editable clip");
  }

  const QVariantList scale_animation{QVariantMap{
      {QStringLiteral("parameter"), QStringLiteral("frameScale")},
      {QStringLiteral("keyframes"), QVariantList{
          QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                      {QStringLiteral("value"), 1.0}},
          QVariantMap{{QStringLiteral("localSeconds"), 1.0 / 30.0},
                      {QStringLiteral("value"), 1.5}}}}}};
  const auto shadow_animation =
      controller.setTimelineClipAnimationForAutomation(clip_id, scale_animation);
  if (!shadow_animation.value(QStringLiteral("ok")).toBool()) {
    return fail("the shadow card should accept the ordinary animated frame scale channel");
  }

  TimelineDocumentController no_shadow_controller;
  no_shadow_controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Shadow-free geometry oracle")},
      {QStringLiteral("width"), 128},
      {QStringLiteral("height"), 96},
      {QStringLiteral("frameRate"), 30.0},
      {QStringLiteral("trackLayout"), QStringLiteral("creator_social")},
  });
  no_shadow_controller.addClipFromGallery(
      QStringLiteral("photo:frame-shadow-oracle"), source_path,
      QStringLiteral("Shadow-free geometry oracle"), 1.0, source_path);
  const auto no_shadow_clip = no_shadow_controller.clips().value(0).toMap();
  const auto no_shadow_clip_id =
      no_shadow_clip.value(QStringLiteral("clipId")).toString();
  const auto no_shadow_frame = no_shadow_controller.setTimelineClipFrameForAutomation(
      no_shadow_clip_id, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
      QStringLiteral("stretch"), 0.5, 0.5, 0.08,
      0.0, 0.0, 0.0, 0.0);
  const auto no_shadow_animation =
      no_shadow_controller.setTimelineClipAnimationForAutomation(
          no_shadow_clip_id, scale_animation);
  if (!no_shadow_frame.value(QStringLiteral("ok")).toBool() ||
      !no_shadow_animation.value(QStringLiteral("ok")).toBool()) {
    return fail("the no-shadow geometry oracle should have the same frame and animation state");
  }

  const auto baseline_pattern =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-baseline-%06d.png"));
  const auto no_shadow_pattern =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-oracle-%06d.png"));
  QString render_error;
  if (!render_timeline_image_sequence(controller, baseline_pattern, &render_error,
                                      0.0, 2.0 / 30.0) ||
      !render_timeline_image_sequence(no_shadow_controller, no_shadow_pattern,
                                      &render_error, 0.0, 2.0 / 30.0)) {
    std::cerr << "timeline_frame_shadow_render_workflow: the shadow frame should render through the native exporter: "
              << render_error.toStdString() << '\n';
    return false;
  }
  const QImage rendered(timeline_image_sequence_frame(baseline_pattern, 1));
  const QImage rendered_scaled(timeline_image_sequence_frame(baseline_pattern, 2));
  const QImage no_shadow_frame_one(timeline_image_sequence_frame(no_shadow_pattern, 1));
  const QImage no_shadow_frame_two(timeline_image_sequence_frame(no_shadow_pattern, 2));
  if (rendered.isNull() || rendered_scaled.isNull() || no_shadow_frame_one.isNull() ||
      no_shadow_frame_two.isNull() || rendered.size() != QSize(128, 96) ||
      rendered_scaled.size() != QSize(128, 96) ||
      no_shadow_frame_one.size() != QSize(128, 96) ||
      no_shadow_frame_two.size() != QSize(128, 96)) {
    return fail("the export should produce a full-size RGBA frame");
  }
  struct CoverageStats final {
    double area = 0.0;
    double centroid = 0.0;
  };
  const auto coverage_stats = [](const QImage& image, bool horizontal,
                                 bool includes_black_shadow) {
    const auto center = horizontal ? image.height() / 2 : image.width() / 2;
    const auto sample_count = horizontal ? image.width() : image.height();
    auto stats = CoverageStats{};
    for (int index = 0; index < sample_count; ++index) {
      const auto pixel = horizontal ? image.pixelColor(index, center)
                                    : image.pixelColor(center, index);
      auto source_coverage = static_cast<double>(pixel.alpha()) / 255.0;
      if (includes_black_shadow) {
        // The source is white and the shadow is black. Multiplying output
        // red by total alpha removes the black backdrop contribution and
        // recovers the foreground's premultiplied coverage independently of
        // the shadow alpha behind antialiased edge pixels.
        source_coverage *= static_cast<double>(pixel.red()) / 255.0;
      }
      stats.area += source_coverage;
      stats.centroid += static_cast<double>(index) * source_coverage;
    }
    if (stats.area > 0.0) stats.centroid /= stats.area;
    return stats;
  };
  const auto compare_geometry = [&](const QImage& with_shadow,
                                    const QImage& without_shadow) {
    const auto shadow_horizontal = coverage_stats(with_shadow, true, true);
    const auto plain_horizontal = coverage_stats(without_shadow, true, false);
    const auto shadow_vertical = coverage_stats(with_shadow, false, true);
    const auto plain_vertical = coverage_stats(without_shadow, false, false);
    return std::abs(shadow_horizontal.area - plain_horizontal.area) <= 0.5 &&
           std::abs(shadow_horizontal.centroid - plain_horizontal.centroid) <= 0.1 &&
           std::abs(shadow_vertical.area - plain_vertical.area) <= 0.5 &&
           std::abs(shadow_vertical.centroid - plain_vertical.centroid) <= 0.1;
  };
  if (!compare_geometry(rendered, no_shadow_frame_one) ||
      !compare_geometry(rendered_scaled, no_shadow_frame_two)) {
    const auto shadow_horizontal = coverage_stats(rendered_scaled, true, true);
    const auto plain_horizontal = coverage_stats(no_shadow_frame_two, true, false);
    const auto shadow_vertical = coverage_stats(rendered_scaled, false, true);
    const auto plain_vertical = coverage_stats(no_shadow_frame_two, false, false);
    std::cerr << "timeline_frame_shadow_render_workflow: shadow composition should preserve foreground coverage area and center at both frame scales; scaled horizontal area/center="
              << shadow_horizontal.area << '/' << shadow_horizontal.centroid
              << " plain=" << plain_horizontal.area << '/' << plain_horizontal.centroid
              << " scaled vertical=" << shadow_vertical.area << '/' << shadow_vertical.centroid
              << " plain=" << plain_vertical.area << '/' << plain_vertical.centroid << '\n';
    return false;
  }

  photo_editor::desktop::timeline_frame_shadow::Parameters high_precision_shadow;
  high_precision_shadow.enabled = true;
  high_precision_shadow.color = QColor(QStringLiteral("#ffff0000"));
  high_precision_shadow.offset_x = -4.0;
  QSize high_precision_size;
  QString high_precision_error;
  auto high_precision_graph =
      photo_editor::desktop::timeline_frame_shadow::ffmpeg_filter(
          high_precision_shadow, QSize(16, 16), QSize(16, 16), QSize(16, 16),
          QStringLiteral("shadow_16bit_test"), QStringLiteral("shadow_16bit_output"),
          &high_precision_size, &high_precision_error, true);
  if (high_precision_graph.isEmpty()) {
    std::cerr << "timeline_frame_shadow_render_workflow: high precision shadow graph should build: "
              << high_precision_error.toStdString() << '\n';
    return false;
  }
  high_precision_graph.replace(QStringLiteral("format=rgba,"),
                               QStringLiteral("format=rgba64le,"));
  high_precision_graph.replace(QStringLiteral("format=rgba["),
                               QStringLiteral("format=rgba64le["));
  QString high_precision_process_error;
  const auto high_precision_frame = process_bytes(
      QStandardPaths::findExecutable(QStringLiteral("ffmpeg")),
      {QStringLiteral("-v"), QStringLiteral("error"),
       QStringLiteral("-f"), QStringLiteral("lavfi"),
       QStringLiteral("-i"), QStringLiteral("color=white:s=16x16:d=1,format=rgba64le"),
       QStringLiteral("-filter_complex"), high_precision_graph,
       QStringLiteral("-map"), QStringLiteral("[shadow_16bit_output]"),
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgba64le"),
       QStringLiteral("pipe:1")},
      &high_precision_process_error);
  const auto read_u16 = [&high_precision_frame](qsizetype byte_offset) {
    const auto low = static_cast<quint8>(high_precision_frame.at(byte_offset));
    const auto high = static_cast<quint8>(high_precision_frame.at(byte_offset + 1));
    return static_cast<quint16>(static_cast<quint16>(low) |
                                (static_cast<quint16>(high) << 8));
  };
  const auto shadow_pixel_offset =
      (12 * high_precision_size.width() + 4) * 8;
  if (high_precision_frame.size() !=
      high_precision_size.width() * high_precision_size.height() * 8) {
    std::cerr << "timeline_frame_shadow_render_workflow: high precision render should produce rgba64le bytes: "
              << high_precision_process_error.toStdString() << '\n';
    return false;
  }
  if (read_u16(shadow_pixel_offset) < 60000 ||
      read_u16(shadow_pixel_offset + 2) > 512 ||
      read_u16(shadow_pixel_offset + 4) > 512 ||
      read_u16(shadow_pixel_offset + 6) < 60000) {
    std::cerr << "timeline_frame_shadow_render_workflow: colored shadows should preserve full-scale RGB in rgba64le; error="
              << high_precision_process_error.toStdString()
              << " rgba16=" << read_u16(shadow_pixel_offset) << ','
              << read_u16(shadow_pixel_offset + 2) << ','
              << read_u16(shadow_pixel_offset + 4) << ','
              << read_u16(shadow_pixel_offset + 6) << '\n';
    return false;
  }

  const auto card_pixel = rendered.pixelColor(48, 48);
  const auto near_shadow_pixel = rendered.pixelColor(20, 40);
  const auto far_shadow_pixel = rendered.pixelColor(8, 40);
  const auto scaled_shadow_pixel = rendered_scaled.pixelColor(8, 36);
  if (card_pixel.alpha() < 250 || card_pixel.red() < 250 ||
      card_pixel.green() < 250 || card_pixel.blue() < 250 ||
      near_shadow_pixel.alpha() < 1 || near_shadow_pixel.alpha() >= 153 ||
      near_shadow_pixel.red() > 8 || near_shadow_pixel.green() > 8 ||
      near_shadow_pixel.blue() > 8 ||
      far_shadow_pixel.alpha() >= near_shadow_pixel.alpha() ||
      scaled_shadow_pixel.alpha() <= far_shadow_pixel.alpha()) {
    std::cerr << "timeline_frame_shadow_render_workflow: export should contain the original card plus a translucent, blurred, offset shadow; card=("
              << card_pixel.red() << ',' << card_pixel.green() << ',' << card_pixel.blue()
              << ',' << card_pixel.alpha() << ") near=("
              << near_shadow_pixel.red() << ',' << near_shadow_pixel.green() << ','
              << near_shadow_pixel.blue() << ',' << near_shadow_pixel.alpha()
              << ") farAlpha=" << far_shadow_pixel.alpha()
              << " scaledAlpha=" << scaled_shadow_pixel.alpha() << '\n';
    return false;
  }

  const QVariantMap no_shadow{
      {QStringLiteral("enabled"), false},
      {QStringLiteral("color"), QStringLiteral("#00000000")},
      {QStringLiteral("offsetX"), 0.0},
      {QStringLiteral("offsetY"), 0.0},
      {QStringLiteral("blurRadius"), 0.0},
      {QStringLiteral("spread"), 0.0},
  };
  const auto reset = controller.setTimelineClipFrameForAutomation(
      clip_id, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0,
      QStringLiteral("stretch"), 0.5, 0.5, 0.08,
      0.0, 0.0, 0.0, 0.0, no_shadow);
  const auto disabled_copy =
      QDir(workspace_path).filePath(QStringLiteral("frame-shadow-disabled.json"));
  const auto disabled_save = controller.saveTimelineCopyForAutomation(disabled_copy);
  QFile disabled_file(disabled_copy);
  if (!reset.value(QStringLiteral("ok")).toBool() ||
      !disabled_save.value(QStringLiteral("ok")).toBool() ||
      !disabled_file.open(QIODevice::ReadOnly) ||
      disabled_file.readAll().contains("\"frameShadow\"")) {
    return fail("resetting an untouched shadow should restore the legacy disabled representation");
  }
  std::cout << "timeline_frame_shadow_render_workflow: shadow survives save/reopen and exports translucent blur/spread with signed offset; default remains legacy-compatible\n";
  return true;
}
