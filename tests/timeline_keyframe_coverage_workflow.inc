bool keyframe_coverage_failure(int line) {
  std::cerr << "keyframe coverage failed at line " << line << '\n';
  return false;
}

bool run_timeline_keyframe_coverage_workflow(const QString& workspace_path) {
  const auto source_path = QDir(workspace_path).filePath(QStringLiteral("keyframe-source.png"));
  QImage source(32, 32, QImage::Format_RGB32);
  source.fill(qRgb(72, 118, 176));
  if (!source.save(source_path)) return keyframe_coverage_failure(__LINE__);

  const auto exercise = [](TimelineDocumentController& controller) {
    const auto original = controller.currentClip();
    const auto parameters = original.value(QStringLiteral("availableKeyframeParameters")).toStringList();
    const auto start = original.value(QStringLiteral("startSeconds")).toDouble();
    if (parameters.isEmpty()) {
      std::cerr << "each editable clip must advertise its supported animation parameters\n";
      return keyframe_coverage_failure(__LINE__);
    }
    for (const auto& parameter : parameters) {
      controller.seekSequenceSeconds(start);
      if (!controller.toggleSelectedClipKeyframe(parameter)) {
        std::cerr << "cannot author first key for " << parameter.toStdString() << '\n';
        return keyframe_coverage_failure(__LINE__);
      }
      controller.seekSequenceSeconds(start + 2.0);
      if (!controller.toggleSelectedClipKeyframe(parameter)) {
        std::cerr << "cannot author second key for " << parameter.toStdString() << '\n';
        return keyframe_coverage_failure(__LINE__);
      }
      for (const auto& mode : {QStringLiteral("hold"), QStringLiteral("ease-in"),
                               QStringLiteral("ease-out"), QStringLiteral("ease-in-out"),
                               QStringLiteral("bezier"), QStringLiteral("linear")}) {
        if (controller.setSelectedClipKeyframeSelectionInterpolation(parameter, {0.0, 2.0}, mode) != 2) {
          std::cerr << "every parameter must accept all curve modes: " << parameter.toStdString() << '\n';
          return keyframe_coverage_failure(__LINE__);
        }
      }
      if (!controller.setSelectedClipKeyframeBezierHandle(parameter, 0.0, QStringLiteral("out"), 0.5, 0.2) ||
          !controller.setSelectedClipKeyframeBezierHandle(parameter, 2.0, QStringLiteral("in"), -0.5, -0.2)) {
        std::cerr << "each parameter must support both weighted tangent handles\n";
        return keyframe_coverage_failure(__LINE__);
      }
    }
    const auto before_batch = controller.currentClip().value(QStringLiteral("keyframes")).toList();
    if (controller.setSelectedClipKeyframeSelectionInterpolation(parameters.first(), {0.0, 2.0},
                                                                QStringLiteral("hold")) != 2) return keyframe_coverage_failure(__LINE__);
    controller.undo();
    if (controller.currentClip().value(QStringLiteral("keyframes")).toList() != before_batch) {
      std::cerr << "one undo must restore both interpolation edits and all weighted handles\n";
      return keyframe_coverage_failure(__LINE__);
    }
    if (!controller.saveTimeline()) return keyframe_coverage_failure(__LINE__);
    TimelineDocumentController reopened;
    if (!reopened.loadTimeline(controller.savedDocumentPath())) return keyframe_coverage_failure(__LINE__);
    reopened.selectClip(controller.currentIndex());
    if (reopened.currentClip().value(QStringLiteral("keyframes")).toList() != before_batch ||
        reopened.currentClip().value(QStringLiteral("availableKeyframeParameters")).toStringList() != parameters) {
      std::cerr << "all layer/effect keys, curves, and discovery options must survive save/reopen\n";
      return keyframe_coverage_failure(__LINE__);
    }
    return true;
  };

  TimelineDocumentController picture;
  picture.createTimeline(QStringLiteral("Picture keyframe coverage"));
  const auto video_path = touch_file(QDir(workspace_path).filePath(QStringLiteral("keys.mp4")));
  picture.addClipFromGallery(QStringLiteral("video:keys"), video_path, QStringLiteral("Picture"), 4.0, source_path);
  const auto picture_id = picture.currentClip().value(QStringLiteral("clipId")).toString();
  for (const auto& effect : {QStringLiteral("Exposure"), QStringLiteral("Contrast"),
                             QStringLiteral("Saturation"), QStringLiteral("Black & White"),
                             QStringLiteral("Blur"), QStringLiteral("Fade"), QStringLiteral("Punch")}) {
    picture.addSelectedClipEffect(effect);
  }
  QStringList layer_ids;
  for (const auto& name : {QStringLiteral("Subject"), QStringLiteral("Background")}) {
    const auto result = picture.addClipColorGradeAdjustmentLayerForAutomation(picture_id, name);
    if (!result.value(QStringLiteral("ok")).toBool()) {
      std::cerr << "grade layer setup failed: " << result.value(QStringLiteral("error")).toString().toStdString() << '\n';
      return keyframe_coverage_failure(__LINE__);
    }
    layer_ids.push_back(result.value(QStringLiteral("layerId")).toString());
  }
  const auto available = picture.currentClip().value(QStringLiteral("availableKeyframeParameters")).toStringList();
  if (available.size() < 70 || !available.contains(QStringLiteral("effect:Blur")) ||
      !available.contains(QStringLiteral("grade:layer:%1:opacity").arg(layer_ids.last())) ||
      available.contains(QStringLiteral("titleFontSize")) ||
      picture.toggleSelectedClipKeyframe(QStringLiteral("effect:Unknown"))) {
    std::cerr << "discovery must cover every installed effect and both independent grade layers\n";
    return keyframe_coverage_failure(__LINE__);
  }
  if (!exercise(picture)) return keyframe_coverage_failure(__LINE__);
  const auto grade_capability = picture.inspectClipColorGradeForAutomation(picture_id);
  if (picture.currentClip().value(QStringLiteral("colorGradeSequenceExportSupported")).toBool() ||
      grade_capability.value(QStringLiteral("sequenceExportSupportsGrade")).toBool()) {
    std::cerr << "animated grades combined with unsupported effects must not advertise export support\n";
    return keyframe_coverage_failure(__LINE__);
  }

  // Large-unit grade controls must not be clamped to a generic +/-6 tangent.
  const auto contrast = QStringLiteral("grade:layer:%1:contrast").arg(layer_ids.first());
  if (!picture.setSelectedClipKeyframeBezierHandle(contrast, 0.0, QStringLiteral("out"), 0.5, 50.0)) return keyframe_coverage_failure(__LINE__);
  const auto contrast_keys = picture.currentClip().value(QStringLiteral("keyframes")).toList();
  for (const auto& value : contrast_keys) {
    const auto key = value.toMap();
    if (key.value(QStringLiteral("parameter")).toString() != contrast ||
        !nearly_equal(key.value(QStringLiteral("seconds")).toDouble(), 0.0)) continue;
    if (!nearly_equal(key.value(QStringLiteral("bezierOutValue")).toDouble(), 50.0)) {
      std::cerr << "grade tangent was clamped to the wrong control range\n";
      return keyframe_coverage_failure(__LINE__);
    }
  }
  const auto edit = picture.editClipKeyframeForAutomation(picture_id, QStringLiteral("set"), contrast, 0.0, 25.0);
  if (!edit.value(QStringLiteral("ok")).toBool() ||
      edit.value(QStringLiteral("interpolation")).toString() != QStringLiteral("bezier")) {
    std::cerr << "value edits must preserve the selected curve mode\n";
    return keyframe_coverage_failure(__LINE__);
  }
  for (const auto& value : picture.currentClip().value(QStringLiteral("keyframes")).toList()) {
    const auto key = value.toMap();
    if (key.value(QStringLiteral("parameter")).toString() == contrast &&
        nearly_equal(key.value(QStringLiteral("seconds")).toDouble(), 0.0) &&
        !nearly_equal(key.value(QStringLiteral("bezierOutValue")).toDouble(), 50.0)) return keyframe_coverage_failure(__LINE__);
  }

  TimelineDocumentController title;
  title.createTimeline(QStringLiteral("Title keyframe coverage"));
  title.addTitleClip();
  title.addSelectedClipEffect(QStringLiteral("Fade"));
  if (!title.currentClip().value(QStringLiteral("availableKeyframeParameters")).toStringList()
          .contains(QStringLiteral("titlePositionX")) || !exercise(title)) return keyframe_coverage_failure(__LINE__);

  TimelineDocumentController graphic;
  graphic.createTimeline(QStringLiteral("Graphic keyframe coverage"));
  if (!graphic.addGraphicShape(QStringLiteral("rectangle"))) return keyframe_coverage_failure(__LINE__);
  graphic.addSelectedClipEffect(QStringLiteral("Exposure"));
  if (!exercise(graphic)) return keyframe_coverage_failure(__LINE__);

  TimelineDocumentController still;
  still.createTimeline(QStringLiteral("Still keyframe coverage"));
  still.addClipFromGallery(QStringLiteral("photo:keys"), source_path, QStringLiteral("Still"), 4.0, source_path);
  for (const auto& effect : {QStringLiteral("Exposure"), QStringLiteral("Contrast"),
                             QStringLiteral("Saturation"), QStringLiteral("Black & White"),
                             QStringLiteral("Blur"), QStringLiteral("Fade"), QStringLiteral("Punch")}) {
    still.addSelectedClipEffect(effect);
  }
  if (!exercise(still)) return keyframe_coverage_failure(__LINE__);

  TimelineDocumentController adjustment;
  adjustment.createTimeline(QStringLiteral("Adjustment keyframe coverage"));
  adjustment.addClipFromGallery(QStringLiteral("photo:adjustment-base"), source_path, QStringLiteral("Base"), 4.0, source_path);
  if (!adjustment.addAdjustmentClip(4.0)) return keyframe_coverage_failure(__LINE__);
  const auto adjustment_id = adjustment.currentClip().value(QStringLiteral("clipId")).toString();
  if (!adjustment.addClipColorGradeAdjustmentLayerForAutomation(adjustment_id, QStringLiteral("Look"))
          .value(QStringLiteral("ok")).toBool()) return keyframe_coverage_failure(__LINE__);
  adjustment.addSelectedClipEffect(QStringLiteral("Punch"));
  const auto adjustment_parameters = adjustment.currentClip().value(QStringLiteral("availableKeyframeParameters")).toStringList();
  if (adjustment_parameters.contains(QStringLiteral("frameScale")) ||
      !adjustment_parameters.contains(QStringLiteral("opacity")) || !exercise(adjustment)) return keyframe_coverage_failure(__LINE__);

  TimelineDocumentController audio;
  audio.createTimeline(QStringLiteral("Audio keyframe coverage"));
  const auto audio_path = touch_file(QDir(workspace_path).filePath(QStringLiteral("keys.wav")));
  audio.addClipFromGallery(QStringLiteral("audio:keys"), audio_path, QStringLiteral("Audio"), 4.0);
  audio.selectClip(0);
  if (audio.currentClip().value(QStringLiteral("availableKeyframeParameters")).toStringList() !=
      QStringList{QStringLiteral("audioGainDb")} || !exercise(audio)) return keyframe_coverage_failure(__LINE__);
  return true;
}
