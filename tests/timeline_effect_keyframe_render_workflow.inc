bool run_timeline_effect_keyframe_render_workflow(const QString& workspace_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) return false;
  const auto proof_root = qEnvironmentVariable("PHOTO_EDITOR_KEYFRAME_PROOF_DIR", workspace_path);
  if (!QDir().mkpath(proof_root)) return false;
  const auto source_path = QDir(proof_root).filePath(QStringLiteral("keyframe-detail-source.png"));
  QImage source(64, 64, QImage::Format_RGB32);
  for (int y = 0; y < 64; ++y) {
    for (int x = 0; x < 64; ++x) {
      const auto value = (x / 4 + y / 4) % 2 == 0 ? 240 : 16;
      source.setPixel(x, y, qRgb(value, value, value));
    }
  }
  if (!source.save(source_path)) return false;
  const auto detail_score = [&ffmpeg](const QString& path, double seconds) {
    const auto pixels = process_bytes(ffmpeg, {
        QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-ss"), QString::number(seconds, 'f', 6),
        QStringLiteral("-i"), path, QStringLiteral("-frames:v"), QStringLiteral("1"),
        QStringLiteral("-vf"), QStringLiteral("scale=64:64"), QStringLiteral("-pix_fmt"), QStringLiteral("gray"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("pipe:1")});
    if (pixels.size() != 64 * 64) return -1.0;
    auto score = 0.0;
    for (int y = 8; y < 56; ++y) {
      for (int x = 8; x < 56; ++x) {
        score += std::abs(static_cast<int>(static_cast<unsigned char>(pixels.at(y * 64 + x))) -
                          static_cast<int>(static_cast<unsigned char>(pixels.at(y * 64 + x - 1))));
      }
    }
    return score / (48 * 48);
  };
  for (const auto& variant : {QStringLiteral("clip-bezier"), QStringLiteral("masked-ease"), QStringLiteral("adjustment-linear")}) {
    TimelineDocumentController controller;
    controller.createTimelineWithSettings({{QStringLiteral("timelineName"), variant},
        {QStringLiteral("width"), 64}, {QStringLiteral("height"), 64}, {QStringLiteral("frameRate"), 24.0}});
    controller.addClipFromGallery(QStringLiteral("photo:detail"), source_path, QStringLiteral("Detail"), 2.0, source_path);
    controller.setDeliverySetting(QStringLiteral("encoderMode"), QStringLiteral("software"));
    if (variant == QStringLiteral("adjustment-linear") && !controller.addAdjustmentClip(2.0)) return false;
    controller.addSelectedClipEffect(QStringLiteral("Blur"));
    controller.seekSequenceSeconds(0.0);
    controller.toggleSelectedClipKeyframe(QStringLiteral("effect:Blur"));
    controller.setSelectedClipEffectAmount(QStringLiteral("Blur"), 0.0);
    controller.seekSequenceSeconds(1.5);
    controller.setSelectedClipEffectAmount(QStringLiteral("Blur"), 1.0);
    if (variant == QStringLiteral("clip-bezier")) {
      controller.setSelectedClipKeyframeBezierHandle(QStringLiteral("effect:Blur"), 0.0, QStringLiteral("out"), 0.5, 0.8);
      controller.setSelectedClipKeyframeBezierHandle(QStringLiteral("effect:Blur"), 1.5, QStringLiteral("in"), -0.5, -0.2);
    } else if (variant == QStringLiteral("masked-ease")) {
      controller.setSelectedClipKeyframeInterpolation(QStringLiteral("effect:Blur"), 0.0, QStringLiteral("ease-in-out"));
      controller.setSelectedClipEffectMask(QStringLiteral("Blur"), {
          {QStringLiteral("enabled"), true}, {QStringLiteral("shape"), QStringLiteral("rectangle")},
          {QStringLiteral("centerX"), 0.5}, {QStringLiteral("centerY"), 0.5},
          {QStringLiteral("width"), 1.0}, {QStringLiteral("height"), 1.0}, {QStringLiteral("feather"), 0.0}});
    }
    const auto output_path = QDir(proof_root).filePath(variant + QStringLiteral(".mp4"));
    if (!controller.renderCurrentTimeline(output_path)) {
      std::cerr << "animated Blur export failed for " << variant.toStdString() << ": "
                << controller.status().toStdString() << '\n';
      return false;
    }
    const auto sharp = detail_score(output_path, 0.0);
    const auto middle = detail_score(output_path, 0.75);
    const auto blurred = detail_score(output_path, 1.75);
    std::cout << variant.toStdString() << " detail first/middle/last: " << sharp << '/' << middle << '/' << blurred << '\n';
    if (sharp < 35.0 || middle < 0.0 || blurred < 0.0 || middle > sharp * 0.4 || blurred > sharp * 0.15) {
      std::cerr << "rendered frames must move from sharp to blurred, including masked and adjustment effects\n";
      return false;
    }
  }
  return true;
}
