namespace {

struct TimelineMaskOraclePoint final {
  double seconds = 0.0;
  double value = 0.0;
};

double timeline_mask_oracle_value_at(
    const QVector<TimelineMaskOraclePoint>& points,
    double seconds) {
  if (points.isEmpty()) {
    return 0.0;
  }
  if (seconds <= points.front().seconds) {
    return points.front().value;
  }
  for (qsizetype index = 0; index + 1 < points.size(); ++index) {
    const auto& left = points.at(index);
    const auto& right = points.at(index + 1);
    if (seconds <= right.seconds) {
      const auto span = right.seconds - left.seconds;
      if (span <= 0.0) {
        return right.value;
      }
      const auto progress = qBound(0.0, (seconds - left.seconds) / span, 1.0);
      return left.value + (right.value - left.value) * progress;
    }
  }
  return points.back().value;
}

double timeline_mask_oracle_ellipse_coverage(
    int x,
    int y,
    const QSize& frame_size,
    double left,
    double top,
    double right,
    double bottom) {
  const auto width_fraction = right - left;
  const auto height_fraction = bottom - top;
  if (width_fraction <= 0.0 || height_fraction <= 0.0) {
    return 0.0;
  }
  const auto radius_x = width_fraction * frame_size.width() * 0.5;
  const auto radius_y = height_fraction * frame_size.height() * 0.5;
  const auto center_x = (left + right) * frame_size.width() * 0.5;
  const auto center_y = (top + bottom) * frame_size.height() * 0.5;
  const auto dx = std::abs((static_cast<double>(x) + 0.5) - center_x);
  const auto dy = std::abs((static_cast<double>(y) + 0.5) - center_y);
  const auto normalized_distance = std::sqrt(
      std::pow(dx / qMax(radius_x, 0.001), 2.0) +
      std::pow(dy / qMax(radius_y, 0.001), 2.0));
  const auto signed_distance =
      (normalized_distance - 1.0) * qMin(radius_x, radius_y);
  return qBound(0.0, 0.5 - signed_distance, 1.0);
}

bool render_timeline_mask_range(
    const TimelineDocumentController& controller,
    const QString& directory,
    double start_seconds,
    double end_seconds,
    int expected_frames,
    QString* output_pattern,
    QString* error_message) {
  if (!QDir().mkpath(directory)) {
    if (error_message) {
      *error_message = QStringLiteral("Unable to create mask pixel output directory.");
    }
    return false;
  }
  const auto pattern = QDir(directory).filePath(QStringLiteral("frame_%06d.png"));
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    if (error_message) {
      *error_message = QStringLiteral("FFmpeg is unavailable.");
    }
    return false;
  }
  auto arguments = photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
      controller, pattern, QStringLiteral("image-sequence"), start_seconds,
      end_seconds, error_message);
  if (arguments.isEmpty()) {
    return false;
  }
  // Four filter-complex workers force GEQ to exercise independent row-slice
  // expression state in the same native render path used by the app.
  arguments.prepend(QStringLiteral("4"));
  arguments.prepend(QStringLiteral("-filter_complex_threads"));
  QProcess process;
  process.start(ffmpeg, arguments);
  if (!process.waitForStarted(5000) || !process.waitForFinished(90000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message) {
      *error_message = QString::fromUtf8(process.readAllStandardError()).trimmed();
    }
    return false;
  }
  const auto frame_count = QDir(directory).entryList(
      {QStringLiteral("frame_*.png")}, QDir::Files).size();
  if (frame_count != expected_frames) {
    if (error_message) {
      *error_message = QStringLiteral("Expected %1 PNG frames, received %2.")
                           .arg(expected_frames)
                           .arg(frame_count);
    }
    return false;
  }
  if (output_pattern) {
    *output_pattern = pattern;
  }
  return true;
}

bool verify_timeline_mask_pixels(
    const QString& pattern,
    const QImage& source,
    const QSize& frame_size,
    int frame_number,
    double sequence_seconds,
    const QHash<QString, QVector<TimelineMaskOraclePoint>>& channels) {
  const auto left = timeline_mask_oracle_value_at(channels.value(QStringLiteral("mask:left")), sequence_seconds);
  const auto top = timeline_mask_oracle_value_at(channels.value(QStringLiteral("mask:top")), sequence_seconds);
  const auto right = timeline_mask_oracle_value_at(channels.value(QStringLiteral("mask:right")), sequence_seconds);
  const auto bottom = timeline_mask_oracle_value_at(channels.value(QStringLiteral("mask:bottom")), sequence_seconds);
  const auto path = timeline_image_sequence_frame(pattern, frame_number);
  const QImage decoded(path);
  if (decoded.isNull() || decoded.size() != frame_size) {
    std::cerr << "Mask pixel oracle could not decode " << path.toStdString()
              << " at t=" << sequence_seconds << '\n';
    return false;
  }

  auto fully_covered_pixels = 0;
  for (int y = 0; y < frame_size.height(); ++y) {
    for (int x = 0; x < frame_size.width(); ++x) {
      const auto coverage = timeline_mask_oracle_ellipse_coverage(
          x, y, frame_size, left, top, right, bottom);
      const auto expected_alpha = static_cast<int>(std::floor(255.0 * coverage));
      const auto actual = decoded.pixelColor(x, y);
      if (qAbs(actual.alpha() - expected_alpha) > 1) {
        std::cerr << "Native GEQ mask alpha differs from the CPU ellipse oracle at frame "
                  << frame_number << " t=" << sequence_seconds
                  << " pixel=" << x << ',' << y << " edges=" << left << ',' << top
                  << ',' << right << ',' << bottom << " expected=" << expected_alpha
                  << " actual=" << actual.alpha() << '\n';
        return false;
      }
      if (expected_alpha == 255) {
        ++fully_covered_pixels;
        const auto expected_rgb = source.pixelColor(x, y);
        if (qAbs(actual.red() - expected_rgb.red()) > 1 ||
            qAbs(actual.green() - expected_rgb.green()) > 1 ||
            qAbs(actual.blue() - expected_rgb.blue()) > 1) {
          std::cerr << "Native GEQ mask should preserve source RGB under full coverage at frame "
                    << frame_number << " t=" << sequence_seconds
                    << " pixel=" << x << ',' << y << " expected="
                    << expected_rgb.red() << ',' << expected_rgb.green() << ','
                    << expected_rgb.blue() << " actual=" << actual.red() << ','
                    << actual.green() << ',' << actual.blue() << '\n';
          return false;
        }
      }
    }
  }
  if (right - left > 0.5 && bottom - top > 0.5 && fully_covered_pixels < 100) {
    std::cerr << "The mask pixel oracle frame should contain a substantial fully covered source area at t="
              << sequence_seconds << " pixels=" << fully_covered_pixels << '\n';
    return false;
  }
  return true;
}

bool compare_timeline_mask_frames(
    const QString& reference_pattern,
    int reference_frame,
    const QString& split_pattern,
    int split_frame,
    double sequence_seconds) {
  const QImage reference(
      timeline_image_sequence_frame(reference_pattern, reference_frame));
  const QImage split(timeline_image_sequence_frame(split_pattern, split_frame));
  if (reference.isNull() || split.isNull() || reference.size() != split.size() ||
      reference != split) {
    std::cerr << "The split PNG render differs from the unsplit render at global t="
              << sequence_seconds << " referenceFrame=" << reference_frame
              << " splitFrame=" << split_frame << '\n';
    return false;
  }
  return true;
}

}  // namespace

bool run_timeline_mask_cache_pixel_workflow(const QString& workspace_path) {
  constexpr int kFrameRate = 30;
  constexpr int kExpectedKeyframeCount = 359;
  constexpr int kTotalFrames = 66;
  constexpr int kFirstRangeFrames = 30;
  constexpr double kRangeStartSeconds = 8.0;
  constexpr double kSplitSeconds = 9.0;
  constexpr double kRangeEndSeconds = 10.2;
  const QSize frame_size(320, 180);
  const auto fixture_path = QDir(
      QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                                .filePath(QStringLiteral(
                                    "fixtures/timeline_native_zoom_scale_v4.json"));
  QFile fixture_file(fixture_path);
  if (!fixture_file.open(QIODevice::ReadOnly)) {
    std::cerr << "The exact dense mask coordinate fixture should be readable: "
              << fixture_path.toStdString() << '\n';
    return false;
  }
  QJsonParseError parse_error;
  const auto fixture = QJsonDocument::fromJson(fixture_file.readAll(), &parse_error)
                           .object();
  const auto fixture_keyframes = fixture.value(QStringLiteral("keyframes")).toArray();
  if (parse_error.error != QJsonParseError::NoError ||
      fixture_keyframes.size() != kExpectedKeyframeCount ||
      fixture.value(QStringLiteral("clipMask")).toObject()
              .value(QStringLiteral("shape")).toString() != QStringLiteral("ellipse")) {
    std::cerr << "The native mask cache fixture must retain its exact 359 keyframes and ellipse geometry\n";
    return false;
  }

  const QStringList parameters{
      QStringLiteral("mask:left"), QStringLiteral("mask:top"),
      QStringLiteral("mask:right"), QStringLiteral("mask:bottom")};
  QHash<QString, QVector<TimelineMaskOraclePoint>> channels;
  QVariantList animation_channels;
  for (const auto& parameter : parameters) {
    QVariantList channel_keyframes;
    auto& oracle_points = channels[parameter];
    for (const auto& value : fixture_keyframes) {
      const auto keyframe = value.toObject();
      if (keyframe.value(QStringLiteral("parameter")).toString() != parameter) {
        continue;
      }
      if (keyframe.value(QStringLiteral("interpolation")).toString() !=
          QStringLiteral("linear")) {
        std::cerr << "The exact dense mask pixel oracle currently expects linear fixture channels\n";
        return false;
      }
      const auto seconds = keyframe.value(QStringLiteral("seconds")).toDouble(-1.0);
      const auto coordinate = keyframe.value(QStringLiteral("value")).toDouble(
          std::numeric_limits<double>::quiet_NaN());
      if (!std::isfinite(seconds) || seconds < 0.0 ||
          !std::isfinite(coordinate) || coordinate < -4.0 || coordinate > 4.0) {
        std::cerr << "A dense mask coordinate keyframe is outside its valid native range\n";
        return false;
      }
      oracle_points.push_back({seconds, coordinate});
      channel_keyframes.push_back(QVariantMap{
          {QStringLiteral("localSeconds"), seconds},
          {QStringLiteral("value"), coordinate},
          {QStringLiteral("interpolation"), QStringLiteral("linear")},
      });
    }
    if (oracle_points.size() < 50) {
      std::cerr << "The exact fixture must contribute dense keyframes to every mask edge; "
                << parameter.toStdString() << " has " << oracle_points.size() << '\n';
      return false;
    }
    animation_channels.push_back(QVariantMap{
        {QStringLiteral("parameter"), parameter},
        {QStringLiteral("keyframes"), channel_keyframes},
    });
  }

  const auto source_path = QDir(workspace_path).filePath(
      QStringLiteral("dense-mask-coordinate-source.png"));
  QImage source(frame_size, QImage::Format_ARGB32);
  for (int y = 0; y < frame_size.height(); ++y) {
    auto* row = reinterpret_cast<QRgb*>(source.scanLine(y));
    for (int x = 0; x < frame_size.width(); ++x) {
      const auto red = (x * 13 + y * 3) & 0xff;
      const auto green = (x * 5 + y * 11) & 0xff;
      const auto blue = (x * 17 + y * 7) & 0xff;
      row[x] = qRgba(red, green, blue, 255);
    }
  }
  if (!source.save(source_path)) {
    std::cerr << "The coordinate-coded mask source image should be writable\n";
    return false;
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Dense GEQ mask pixel oracle")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), static_cast<double>(kFrameRate)},
  });
  controller.addClipFromGallery(
      QStringLiteral("photo:dense-mask-cache-pixel-oracle"), source_path,
      QStringLiteral("Dense mask pixel source"), 12.0, source_path);
  const auto clip_id = controller.clips().value(0).toMap()
                           .value(QStringLiteral("clipId")).toString();
  auto mask = fixture.value(QStringLiteral("clipMask")).toObject();
  auto mask_values = QVariantMap{
      {QStringLiteral("shape"), mask.value(QStringLiteral("shape")).toString()},
      {QStringLiteral("left"), mask.value(QStringLiteral("left")).toDouble()},
      {QStringLiteral("top"), mask.value(QStringLiteral("top")).toDouble()},
      {QStringLiteral("right"), mask.value(QStringLiteral("right")).toDouble()},
      {QStringLiteral("bottom"), mask.value(QStringLiteral("bottom")).toDouble()},
      {QStringLiteral("cornerRadius"), mask.value(QStringLiteral("cornerRadius")).toDouble()},
  };
  if (clip_id.isEmpty() ||
      !controller.setTimelineClipMaskForAutomation(clip_id, mask_values)
           .value(QStringLiteral("ok")).toBool()) {
    std::cerr << "The native timeline should accept the exact fixture mask geometry\n";
    return false;
  }
  const auto animation_result =
      controller.setTimelineClipAnimationForAutomation(clip_id, animation_channels);
  if (!animation_result.value(QStringLiteral("ok")).toBool()) {
    std::cerr << "The native timeline should accept the exact dense mask channels: "
              << animation_result.value(QStringLiteral("error")).toString().toStdString()
              << '\n';
    return false;
  }

  QString whole_pattern;
  QString first_pattern;
  QString second_pattern;
  QString render_error;
  if (!render_timeline_mask_range(
          controller, QDir(workspace_path).filePath(QStringLiteral("mask-whole-range")),
          kRangeStartSeconds, kRangeEndSeconds, kTotalFrames, &whole_pattern,
          &render_error) ||
      !render_timeline_mask_range(
          controller, QDir(workspace_path).filePath(QStringLiteral("mask-first-range")),
          kRangeStartSeconds, kSplitSeconds, kFirstRangeFrames, &first_pattern,
          &render_error) ||
      !render_timeline_mask_range(
          controller, QDir(workspace_path).filePath(QStringLiteral("mask-second-range")),
          kSplitSeconds, kRangeEndSeconds, kTotalFrames - kFirstRangeFrames,
          &second_pattern, &render_error)) {
    std::cerr << "Dense native mask PNG range rendering should succeed: "
              << render_error.toStdString() << '\n';
    return false;
  }

  for (int frame_index = 0; frame_index < kTotalFrames; ++frame_index) {
    const auto sequence_seconds =
        kRangeStartSeconds + static_cast<double>(frame_index) / kFrameRate;
    const auto frame_number = frame_index + 1;
    if (!verify_timeline_mask_pixels(
            whole_pattern, source, frame_size, frame_number,
            sequence_seconds, channels)) {
      return false;
    }
    const auto split_pattern = frame_index < kFirstRangeFrames
                                   ? first_pattern
                                   : second_pattern;
    const auto split_frame = frame_index < kFirstRangeFrames
                                 ? frame_number
                                 : frame_index - kFirstRangeFrames + 1;
    if (!compare_timeline_mask_frames(
            whole_pattern, frame_number, split_pattern, split_frame,
            sequence_seconds)) {
      return false;
    }
  }
  std::cout << "timeline_mask_cache_pixel_frames=" << kTotalFrames
            << " rangeSplitGlobalFrame=" << kFirstRangeFrames
            << " keyframeCount=" << fixture_keyframes.size() << '\n';
  return true;
}
