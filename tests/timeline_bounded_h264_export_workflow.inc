#include <QDirIterator>
#include <QMap>

#include "timeline_bounded_ffmpeg_memory_policy.h"

namespace {

QByteArray decode_video_frame_rgb24(
    const QString& ffmpeg,
    const QString& path,
    qint64 frame_index,
    const QSize& frame_size) {
  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-i"), path,
          QStringLiteral("-vf"),
          QStringLiteral("select=eq(n\\,%1)").arg(frame_index),
          QStringLiteral("-fps_mode"), QStringLiteral("vfr"),
          QStringLiteral("-frames:v"), QStringLiteral("1"),
          QStringLiteral("-f"), QStringLiteral("rawvideo"),
          QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
          QStringLiteral("pipe:1"),
      });
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  const auto bytes = process.readAllStandardOutput();
  const auto expected_size = static_cast<qint64>(frame_size.width()) *
                             frame_size.height() * 3;
  return bytes.size() == expected_size ? bytes : QByteArray{};
}

qint64 count_video_frames(const QString& ffprobe, const QString& path) {
  bool ok = false;
  const auto result = process_stdout(
      ffprobe,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-select_streams"), QStringLiteral("v:0"),
          QStringLiteral("-count_frames"),
          QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_frames"),
          QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"), path,
      })
                          .trimmed()
                          .toLongLong(&ok);
  return ok ? result : -1;
}

bool has_bounded_sdr_limited_range_conversion(
    const QStringList& arguments,
    QString* graph_text = nullptr) {
  auto graph = QString{};
  const auto inline_graph_index = arguments.indexOf(QStringLiteral("-filter_complex"));
  const auto sidecar_index = arguments.indexOf(QStringLiteral("-filter_complex_script"));
  if (inline_graph_index >= 0 && inline_graph_index + 1 < arguments.size()) {
    graph = arguments.at(inline_graph_index + 1);
  } else if (sidecar_index >= 0 && sidecar_index + 1 < arguments.size()) {
    QFile graph_file(arguments.at(sidecar_index + 1));
    if (graph_file.open(QIODevice::ReadOnly)) {
      graph = QString::fromUtf8(graph_file.readAll());
    }
  }
  if (graph_text != nullptr) {
    *graph_text = graph;
  }
  return graph.contains(QStringLiteral(
      "scale=in_range=full:out_range=limited:in_color_matrix=bt709:"
      "out_color_matrix=bt709,format=yuv420p,setparams=range=limited"));
}

bool run_bounded_concat_frame_count_oracle(
    const QString& ffmpeg,
    const QString& ffprobe,
    const QString& workspace_path) {
  using photo_editor::desktop::timeline_motion::bounded_h264::
      build_final_video_audio_mux_arguments;
  const auto directory = QDir(workspace_path).filePath(QStringLiteral("bounded-concat-oracle"));
  if (!QDir().mkpath(directory)) {
    std::cerr << "Could not create the bounded-concat frame-count oracle directory\n";
    return false;
  }
  const auto run_ffmpeg = [ffmpeg](const QStringList& arguments,
                                   const QString& phase) {
    QProcess process;
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      std::cerr << "The " << phase.toStdString() << " fixture failed: "
                << process.readAllStandardError().toStdString() << '\n';
      return false;
    }
    return true;
  };

  constexpr int kFrameRate = 30;
  constexpr qint64 kTotalFrames = 300;
  const QVector<int> chunk_frame_counts{96, 96, 108};
  const QVector<QString> colors{QStringLiteral("red"), QStringLiteral("green"),
                                QStringLiteral("blue")};
  QStringList concat_lines{QStringLiteral("ffconcat version 1.0")};
  for (qsizetype index = 0; index < chunk_frame_counts.size(); ++index) {
    const auto frame_count = chunk_frame_counts.at(index);
    const auto chunk_name = QStringLiteral("bframes-%1.mp4")
                                .arg(static_cast<qlonglong>(index), 2, 10, QLatin1Char('0'));
    const auto chunk_path = QDir(directory).filePath(chunk_name);
    const auto lavfi = QStringLiteral("color=c=%1:s=128x72:r=%2")
                           .arg(colors.at(index))
                           .arg(kFrameRate);
    if (!run_ffmpeg({
            QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
            QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"), lavfi,
            QStringLiteral("-frames:v"), QString::number(frame_count),
            QStringLiteral("-c:v"), QStringLiteral("libx264"),
            QStringLiteral("-profile:v"), QStringLiteral("high"),
            QStringLiteral("-bf"), QStringLiteral("3"),
            QStringLiteral("-threads"), QStringLiteral("1"),
            QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
            QStringLiteral("-r"), QString::number(kFrameRate),
            QStringLiteral("-fps_mode"), QStringLiteral("cfr"),
            QStringLiteral("-video_track_timescale"), QStringLiteral("240000"),
            QStringLiteral("-y"), chunk_path,
        }, QStringLiteral("B-frame chunk")) ||
        count_video_frames(ffprobe, chunk_path) != frame_count) {
      std::cerr << "Each bounded concat fixture chunk should preserve its requested frames\n";
      return false;
    }
    concat_lines.push_back(QStringLiteral("file '%1'").arg(chunk_name));
  }

  const auto concat_path = QDir(directory).filePath(QStringLiteral("video.ffconcat"));
  QSaveFile concat_file(concat_path);
  const auto concat_bytes =
      (concat_lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
  if (!concat_file.open(QIODevice::WriteOnly) ||
      concat_file.write(concat_bytes) != concat_bytes.size() || !concat_file.commit()) {
    std::cerr << "The bounded concat fixture playlist should be writable\n";
    return false;
  }
  const auto audio_path = QDir(directory).filePath(QStringLiteral("audio.wav"));
  if (!run_ffmpeg({
          QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
          QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
          QStringLiteral("anullsrc=channel_layout=stereo:sample_rate=48000"),
          QStringLiteral("-t"), QStringLiteral("10"),
          QStringLiteral("-c:a"), QStringLiteral("pcm_s24le"),
          QStringLiteral("-ar"), QStringLiteral("48000"),
          QStringLiteral("-y"), audio_path,
      }, QStringLiteral("continuous audio"))) {
    return false;
  }

  const auto output_path = QDir(directory).filePath(QStringLiteral("joined.mp4"));
  QString argument_error;
  const auto mux_arguments = build_final_video_audio_mux_arguments(
      concat_path, audio_path, output_path, kTotalFrames, 48000,
      QStringLiteral("192k"), &argument_error);
  if (!argument_error.isEmpty() || mux_arguments.isEmpty() ||
      mux_arguments.contains(QStringLiteral("-shortest")) ||
      !mux_arguments.contains(QStringLiteral("-frames:v")) ||
      mux_arguments.value(mux_arguments.indexOf(QStringLiteral("-frames:v")) + 1) !=
          QString::number(kTotalFrames)) {
    std::cerr << "The bounded final mux should preserve video frame clock independently of AAC packet timing: "
              << argument_error.toStdString() << '\n';
    return false;
  }
  if (!run_ffmpeg(mux_arguments, QStringLiteral("B-frame final mux"))) {
    return false;
  }
  const auto joined_frame_count = count_video_frames(ffprobe, output_path);
  bool duration_ok = false;
  const auto joined_duration = process_stdout(
      ffprobe,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-select_streams"), QStringLiteral("v:0"),
          QStringLiteral("-show_entries"), QStringLiteral("stream=duration"),
          QStringLiteral("-of"), QStringLiteral("default=noprint_wrappers=1:nokey=1"),
          output_path,
      })
                                  .trimmed()
                                  .toDouble(&duration_ok);
  if (joined_frame_count != kTotalFrames || !duration_ok ||
      qAbs(joined_duration - 10.0) > 1.0e-5) {
    std::cerr << "The bounded B-frame concat should retain all 300 frames and 10 seconds; got "
              << joined_frame_count << " frames / " << joined_duration << " seconds\n";
    return false;
  }
  return true;
}

bool run_bounded_rational_frame_clock_oracle(
    const QString& ffmpeg,
    const QString& ffprobe,
    const QString& workspace_path) {
  using photo_editor::desktop::timeline_motion::bounded_h264::
      rational_frame_clock_filter;
  const auto directory = QDir(workspace_path).filePath(
      QStringLiteral("bounded-rational-frame-clock-oracle"));
  if (!QDir().mkpath(directory)) {
    std::cerr << "Could not create the rational frame-clock fixture directory\n";
    return false;
  }

  constexpr int kFrameRate = 30;
  constexpr qint64 kExpectedFrames = 32;
  constexpr int kWidth = 64;
  constexpr int kHeight = 64;
  constexpr int kBytesPerPixel = 3;
  const QVector<int> source_frame_counts{8, 16, 8};
  auto global_frame_index = 0;
  QStringList input_arguments;
  for (qsizetype segment = 0; segment < source_frame_counts.size(); ++segment) {
    const auto raw_path = QDir(directory).filePath(
        QStringLiteral("source-%1.rgb").arg(static_cast<qlonglong>(segment)));
    QFile raw_file(raw_path);
    if (!raw_file.open(QIODevice::WriteOnly)) {
      std::cerr << "Could not create a source segment for the frame-order fixture\n";
      return false;
    }
    const auto frame_size = kWidth * kHeight * kBytesPerPixel;
    for (auto frame = 0; frame < source_frame_counts.at(segment); ++frame) {
      const auto intensity = 20 + global_frame_index * 6;
      const QByteArray pixels(frame_size, static_cast<char>(intensity));
      if (raw_file.write(pixels) != pixels.size()) {
        std::cerr << "Could not write a source marker frame\n";
        return false;
      }
      ++global_frame_index;
    }
    raw_file.close();
    input_arguments << QStringLiteral("-f") << QStringLiteral("rawvideo")
                    << QStringLiteral("-pixel_format") << QStringLiteral("rgb24")
                    << QStringLiteral("-video_size")
                    << QStringLiteral("%1x%2").arg(kWidth).arg(kHeight)
                    << QStringLiteral("-framerate") << QString::number(kFrameRate)
                    << QStringLiteral("-i") << raw_path;
  }
  const auto exact_clock = rational_frame_clock_filter(kFrameRate);
  if (exact_clock != QStringLiteral("settb=expr=1/30,setpts=N")) {
    std::cerr << "The integer-rate frame clock should use an exact 1/30 timebase\n";
    return false;
  }
  if (rational_frame_clock_filter(29.97) !=
          QStringLiteral("settb=expr=1/2997,setpts=N*100") ||
      rational_frame_clock_filter(23.976) !=
          QStringLiteral("settb=expr=1/2997,setpts=N*125")) {
    std::cerr << "Common fractional frame rates should retain their exact rational frame periods\n";
    return false;
  }
  const auto graph = QStringLiteral(
      "[0:v]format=yuv420p,settb=1/1000,"
      "setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=0.033333333[s0];"
      "[1:v]format=yuv420p,settb=1/1000,"
      "setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=0.033333333[s1];"
      "[2:v]format=yuv420p,settb=1/1000,"
      "setpts=PTS-STARTPTS,tpad=stop_mode=clone:stop_duration=0.033333333[s2];"
      "[s0][s1][s2]concat=n=3:v=1:a=0[vconcat];"
      "[vconcat]select='not(eq(n,8)+eq(n,25)+eq(n,34))',%1,format=yuv420p[outv]")
                          .arg(exact_clock);
  const auto graph_path = QDir(directory).filePath(QStringLiteral("multi-cut.ffgraph"));
  QSaveFile graph_file(graph_path);
  const auto graph_bytes = graph.toUtf8();
  if (!graph_file.open(QIODevice::WriteOnly) ||
      graph_file.write(graph_bytes) != graph_bytes.size() || !graph_file.commit()) {
    std::cerr << "Could not write the rational frame-clock fixture graph\n";
    return false;
  }
  const auto output_path = QDir(directory).filePath(QStringLiteral("multi-cut.mp4"));
  QStringList render_arguments{
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
  };
  render_arguments.append(input_arguments);
  render_arguments << QStringLiteral("-filter_complex_script") << graph_path
                   << QStringLiteral("-map") << QStringLiteral("[outv]")
                   << QStringLiteral("-frames:v") << QString::number(kExpectedFrames)
                   << QStringLiteral("-r") << QString::number(kFrameRate)
                   << QStringLiteral("-fps_mode") << QStringLiteral("cfr")
                   << QStringLiteral("-threads") << QStringLiteral("1")
                   << QStringLiteral("-c:v") << QStringLiteral("libx264")
                   << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
                   << QStringLiteral("-video_track_timescale") << QStringLiteral("240000")
                   << QStringLiteral("-y") << output_path;
  QProcess render;
  render.start(ffmpeg, render_arguments);
  if (!render.waitForStarted(5000) || !render.waitForFinished(30000) ||
      render.exitStatus() != QProcess::NormalExit || render.exitCode() != 0) {
    std::cerr << "The rational frame-clock FFmpeg fixture failed: "
              << render.readAllStandardError().toStdString() << '\n';
    return false;
  }

  QProcess probe;
  probe.start(ffprobe, {
      QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-count_frames"), QStringLiteral("-select_streams"),
      QStringLiteral("v:0"), QStringLiteral("-show_entries"),
      QStringLiteral("stream=avg_frame_rate,duration,nb_read_frames"),
      QStringLiteral("-of"), QStringLiteral("json"), output_path,
  });
  if (!probe.waitForStarted(5000) || !probe.waitForFinished(30000) ||
      probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
    std::cerr << "FFprobe could not inspect the rational frame-clock fixture\n";
    return false;
  }
  QJsonParseError parse_error;
  const auto parsed = QJsonDocument::fromJson(probe.readAllStandardOutput(), &parse_error);
  const auto streams = parsed.object().value(QStringLiteral("streams")).toArray();
  if (parse_error.error != QJsonParseError::NoError || streams.isEmpty()) {
    std::cerr << "FFprobe returned no video stream data for the rational frame-clock fixture\n";
    return false;
  }
  const auto stream = streams.first().toObject();
  bool frame_count_ok = false;
  const auto frame_count = stream.value(QStringLiteral("nb_read_frames"))
                               .toString().toLongLong(&frame_count_ok);
  bool duration_ok = false;
  const auto duration = stream.value(QStringLiteral("duration"))
                            .toString().toDouble(&duration_ok);
  if (parse_error.error != QJsonParseError::NoError || !frame_count_ok ||
      frame_count != kExpectedFrames ||
      stream.value(QStringLiteral("avg_frame_rate")).toString() != QStringLiteral("30/1") ||
      !duration_ok || qAbs(duration - 32.0 / 30.0) > 1.0e-6) {
    std::cerr << "The 1ms-input-timebase cut fixture should retain 32 frames at exact 30 fps; got "
              << frame_count << " frames, rate "
              << stream.value(QStringLiteral("avg_frame_rate")).toString().toStdString()
              << ", duration " << duration << "\n";
    return false;
  }

  QProcess decoded;
  decoded.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-i"), output_path,
      QStringLiteral("-fps_mode"), QStringLiteral("passthrough"),
      QStringLiteral("-f"), QStringLiteral("rawvideo"),
      QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
      QStringLiteral("pipe:1"),
  });
  if (!decoded.waitForStarted(5000) || !decoded.waitForFinished(30000) ||
      decoded.exitStatus() != QProcess::NormalExit || decoded.exitCode() != 0) {
    std::cerr << "FFmpeg could not decode the rational frame-clock fixture\n";
    return false;
  }
  const auto decoded_pixels = decoded.readAllStandardOutput();
  const auto decoded_frame_bytes = kWidth * kHeight * kBytesPerPixel;
  if (decoded_pixels.size() != decoded_frame_bytes * kExpectedFrames) {
    std::cerr << "The rational frame-clock fixture decoded a partial or extra frame\n";
    return false;
  }
  for (auto frame = 0; frame < kExpectedFrames; ++frame) {
    const auto offset = frame * decoded_frame_bytes +
                        (kHeight / 2 * kWidth + kWidth / 2) * kBytesPerPixel;
    const auto red = static_cast<unsigned char>(decoded_pixels.at(offset));
    const auto green = static_cast<unsigned char>(decoded_pixels.at(offset + 1));
    const auto blue = static_cast<unsigned char>(decoded_pixels.at(offset + 2));
    const auto expected_intensity = 20 + frame * 6;
    const auto within_marker_tolerance =
        std::abs(static_cast<int>(red) - expected_intensity) <= 3 &&
        std::abs(static_cast<int>(green) - expected_intensity) <= 3 &&
        std::abs(static_cast<int>(blue) - expected_intensity) <= 3;
    const auto channels_remain_neutral =
        std::abs(static_cast<int>(red) - static_cast<int>(green)) <= 4 &&
        std::abs(static_cast<int>(green) - static_cast<int>(blue)) <= 4;
    if (!within_marker_tolerance || !channels_remain_neutral) {
      std::cerr << "The exact frame clock changed or reordered the unique source marker at frame "
                << frame << "\n";
      return false;
    }
  }
  return true;
}

bool run_guard_frame_removal_pixel_order_oracle(
    const QString& ffmpeg) {
  constexpr int kFrameRate = 30;
  const auto frame_size = QSize(128, 72);
  const auto frame_bytes = frame_size.width() * frame_size.height() * 3;
  const QVector<QString> colors{
      QStringLiteral("red"), QStringLiteral("green"),
      QStringLiteral("blue"), QStringLiteral("yellow"),
      QStringLiteral("magenta"), QStringLiteral("cyan"),
      QStringLiteral("white"), QStringLiteral("black")};
  const auto run_rawvideo = [ffmpeg](const QStringList& arguments,
                                     const QString& phase) -> QByteArray {
    QProcess process;
    process.start(ffmpeg, arguments);
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
        process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
      std::cerr << "The " << phase.toStdString() << " guard-frame oracle failed: "
                << process.readAllStandardError().toStdString() << '\n';
      return {};
    }
    return process.readAllStandardOutput();
  };
  const auto raw_output_arguments = [](int expected_frames) {
    return QStringList{
        QStringLiteral("-map"), QStringLiteral("[outv]"),
        QStringLiteral("-frames:v"), QString::number(expected_frames),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
        QStringLiteral("pipe:1")};
  };

  QStringList one_frame_inputs;
  QStringList padded_segments;
  QStringList one_frame_guard_indices;
  for (qsizetype index = 0; index < colors.size(); ++index) {
    const auto color = colors.at(index);
    one_frame_inputs << QStringLiteral("-f") << QStringLiteral("lavfi")
                     << QStringLiteral("-i")
                     << QStringLiteral("color=c=%1:s=%2x%3:r=%4")
                            .arg(color)
                            .arg(frame_size.width())
                            .arg(frame_size.height())
                            .arg(kFrameRate);
    padded_segments << QStringLiteral(
                           "[%1:v]trim=end_frame=1,setpts=PTS-STARTPTS,"
                           "tpad=stop_mode=clone:stop_duration=%2[segment%1]")
                           .arg(index)
                           .arg(QString::number(1.0 / kFrameRate, 'f', 9));
    one_frame_guard_indices.push_back(QString::number(index * 2 + 1));
  }
  const auto one_frame_concat_inputs = [&colors](const QString& prefix) {
    QStringList labels;
    for (qsizetype index = 0; index < colors.size(); ++index) {
      labels.push_back(QStringLiteral("[%1%2]").arg(prefix).arg(index));
    }
    return labels.join(QString{});
  };
  auto padded_graph = padded_segments.join(QLatin1Char(';')) + QLatin1Char(';') +
      one_frame_concat_inputs(QStringLiteral("segment")) +
      QStringLiteral("concat=n=%1:v=1:a=0[vconcat];")
          .arg(colors.size()) +
      QStringLiteral("[vconcat]select='not(%1)',setpts=N/(%2*TB)[outv]")
          .arg([&one_frame_guard_indices]() {
            QStringList terms;
            for (const auto& index : one_frame_guard_indices) {
              terms.push_back(QStringLiteral("eq(n,%1)").arg(index));
            }
            return terms.join(QLatin1Char('+'));
          }())
          .arg(kFrameRate);
  const auto padded_bytes = run_rawvideo(
      one_frame_inputs + QStringList{
          QStringLiteral("-filter_complex"), padded_graph} +
          raw_output_arguments(static_cast<int>(colors.size())),
      QStringLiteral("eight one-frame segments"));

  QByteArray expected_bytes;
  for (const auto& color : colors) {
    const auto frame = run_rawvideo(
        {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
         QStringLiteral("color=c=%1:s=%2x%3:r=%4")
             .arg(color).arg(frame_size.width()).arg(frame_size.height()).arg(kFrameRate),
         QStringLiteral("-vf"), QStringLiteral("trim=end_frame=1"),
         QStringLiteral("-frames:v"), QStringLiteral("1"),
         QStringLiteral("-f"), QStringLiteral("rawvideo"),
         QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
         QStringLiteral("pipe:1")},
        QStringLiteral("individual one-frame expected source"));
    expected_bytes.append(frame);
  }
  if (padded_bytes.size() != frame_bytes * colors.size() ||
      expected_bytes.size() != padded_bytes.size() || padded_bytes != expected_bytes) {
    std::cerr << "Removing the eight one-frame clone guards should preserve each distinct pixel frame in order; got "
              << padded_bytes.size() << " bytes, expected "
              << frame_bytes * colors.size() << '\n';
    return false;
  }

  // Model a review range whose sequence origin is frame 43: a one-frame
  // marker segment followed by 16 frames selected from the global source
  // clock. The guard is at output-frame indexes 1 and 18 in the padded
  // concat. Compare every decoded pixel against the unpadded global-time
  // reference, so duplicates, gaps, or range-local animation resets fail.
  QStringList mixed_inputs{
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=orange:s=%1x%2:r=%3")
          .arg(frame_size.width()).arg(frame_size.height()).arg(kFrameRate),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("testsrc2=size=%1x%2:rate=%3:duration=3")
          .arg(frame_size.width()).arg(frame_size.height()).arg(kFrameRate)};
  const auto mixed_padded_graph = QStringLiteral(
      "[0:v]trim=end_frame=1,setpts=PTS-STARTPTS,"
      "tpad=stop_mode=clone:stop_duration=%2[marker];"
      "[1:v]trim=start_frame=43:end_frame=59,setpts=PTS-STARTPTS,"
      "tpad=stop_mode=clone:stop_duration=%2[moving];"
      "[marker][moving]concat=n=2:v=1:a=0[vconcat];"
      "[vconcat]select='not(eq(n,1)+eq(n,18))',setpts=N/(%1*TB)[outv]")
                                    .arg(kFrameRate)
                                    .arg(QString::number(1.0 / kFrameRate, 'f', 9));
  const auto mixed_padded_bytes = run_rawvideo(
      mixed_inputs + QStringList{
          QStringLiteral("-filter_complex"), mixed_padded_graph} +
          raw_output_arguments(17),
      QStringLiteral("nonzero-origin 1+16 segment sequence"));
  const auto expected_marker = run_rawvideo(
      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("color=c=orange:s=%1x%2:r=%3")
           .arg(frame_size.width()).arg(frame_size.height()).arg(kFrameRate),
       QStringLiteral("-vf"), QStringLiteral("trim=end_frame=1"),
       QStringLiteral("-frames:v"), QStringLiteral("1"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
       QStringLiteral("pipe:1")},
      QStringLiteral("nonzero-origin marker expected source"));
  const auto expected_global_frames = run_rawvideo(
      {QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
       QStringLiteral("testsrc2=size=%1x%2:rate=%3:duration=3")
           .arg(frame_size.width()).arg(frame_size.height()).arg(kFrameRate),
       QStringLiteral("-vf"),
       QStringLiteral("trim=start_frame=43:end_frame=59,setpts=PTS-STARTPTS"),
       QStringLiteral("-frames:v"), QStringLiteral("16"),
       QStringLiteral("-f"), QStringLiteral("rawvideo"),
       QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
       QStringLiteral("pipe:1")},
      QStringLiteral("global source frames 43 through 58"));
  const auto mixed_expected_bytes = expected_marker + expected_global_frames;
  if (mixed_padded_bytes.size() != frame_bytes * 17 ||
      mixed_expected_bytes.size() != mixed_padded_bytes.size() ||
      mixed_padded_bytes != mixed_expected_bytes) {
    std::cerr << "Removing guards around the nonzero-origin 1+16 segments should retain marker plus global frames [43,59) exactly; got "
              << mixed_padded_bytes.size() << " bytes, expected "
              << frame_bytes * 17 << '\n';
    return false;
  }
  return true;
}

QByteArray decode_all_video_frames_rgb24(
    const QString& ffmpeg,
    const QString& path) {
  QProcess process;
  process.start(
      ffmpeg,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-i"), path,
          QStringLiteral("-f"), QStringLiteral("rawvideo"),
          QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
          QStringLiteral("pipe:1"),
      });
  if (!process.waitForStarted(5000) || !process.waitForFinished(30000) ||
      process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return {};
  }
  return process.readAllStandardOutput();
}

bool assert_seam_frames_close(
    const QString& ffmpeg,
    const QString& reference_path,
    const QString& bounded_path,
    const QSize& frame_size,
    const QVector<qint64>& frame_indices) {
  for (const auto frame_index : frame_indices) {
    const auto reference = decode_video_frame_rgb24(
        ffmpeg, reference_path, frame_index, frame_size);
    const auto bounded = decode_video_frame_rgb24(
        ffmpeg, bounded_path, frame_index, frame_size);
    if (reference.isEmpty() || bounded.isEmpty() || reference.size() != bounded.size()) {
      std::cerr << "Could not decode frame " << frame_index
                << " from both whole-timeline and bounded H.264 output\n";
      return false;
    }
    quint64 absolute_error = 0;
    for (qsizetype byte_index = 0; byte_index < reference.size(); ++byte_index) {
      absolute_error += static_cast<quint64>(std::abs(
          static_cast<int>(static_cast<unsigned char>(reference.at(byte_index))) -
          static_cast<int>(static_cast<unsigned char>(bounded.at(byte_index)))));
    }
    const auto mean_error = static_cast<double>(absolute_error) /
                            static_cast<double>(reference.size());
    if (mean_error > 24.0) {
      std::cerr << "Frame " << frame_index
                << " diverged at a bounded render seam; mean RGB error=" << mean_error << '\n';
      return false;
    }
  }
  return true;
}

bool run_bounded_subframe_video_timing_oracle(
    const QString& ffmpeg,
    const QString& ffprobe,
    const QString& workspace_path) {
  constexpr int kFramesPerSecond = 30;
  constexpr double kClipDurationSeconds = 0.0625;
  constexpr int kClipCount = 16;
  constexpr int kExpectedFrameCount = kFramesPerSecond;
  const QSize frame_size(128, 72);
  const QVector<QColor> alternating_colors{
      QColor(230, 32, 32), QColor(32, 64, 230)};

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Subframe video timing oracle")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), static_cast<double>(kFramesPerSecond)},
  });
  const auto track = controller.createTimelineTrackForAutomation(
      QStringLiteral("video"), QStringLiteral("Fast cuts"), {});
  const auto track_id = track.value(QStringLiteral("trackId")).toString();
  if (!track.value(QStringLiteral("ok")).toBool() || track_id.isEmpty()) {
    std::cerr << "The subframe timing oracle could not create its native video lane: "
              << track.value(QStringLiteral("error")).toString().toStdString() << '\n';
    return false;
  }

  QHash<QString, QColor> color_by_clip_id;
  for (int index = 0; index < kClipCount; ++index) {
    const auto color = alternating_colors.at(index % alternating_colors.size());
    const auto source_path = QDir(workspace_path).filePath(
        QStringLiteral("subframe-color-%1.png").arg(index, 2, 10, QLatin1Char('0')));
    QImage source(frame_size, QImage::Format_RGB32);
    source.fill(color);
    if (!source.save(source_path)) {
      std::cerr << "The subframe timing oracle could not save color source "
                << index << '\n';
      return false;
    }
    const auto start_seconds = static_cast<double>(index) * kClipDurationSeconds;
    const auto created = controller.createTimelineClipForAutomation(
        track_id,
        QStringLiteral("photo:subframe-oracle-%1").arg(index),
        source_path,
        QStringLiteral("Color %1").arg(index),
        kClipDurationSeconds,
        0.0,
        kClipDurationSeconds,
        start_seconds,
        {});
    const auto clip_id = created.value(QStringLiteral("createdClipId")).toString();
    if (!created.value(QStringLiteral("ok")).toBool() || clip_id.isEmpty()) {
      std::cerr << "The subframe timing oracle could not add clip " << index
                << ": " << created.value(QStringLiteral("error")).toString().toStdString()
                << '\n';
      return false;
    }
    color_by_clip_id.insert(clip_id, color);
  }
  if (qAbs(controller.totalDurationSecondsValue() - 1.0) > 1.0e-9) {
    std::cerr << "The subframe timing oracle should span exactly one second\n";
    return false;
  }

  QVector<QColor> expected_frame_colors;
  expected_frame_colors.reserve(kExpectedFrameCount);
  for (int frame_index = 0; frame_index < kExpectedFrameCount; ++frame_index) {
    // Clear selection so the Program's active clip is resolved from the global
    // sequence playhead, matching the preview's 30 fps frame sampling.
    controller.selectClip(-1);
    controller.seekSequenceSeconds(
        static_cast<double>(frame_index) / kFramesPerSecond);
    const auto active_clip_id = controller.programClip()
                                    .value(QStringLiteral("clipId")).toString();
    if (!color_by_clip_id.contains(active_clip_id)) {
      std::cerr << "The native preview has no active source at global frame "
                << frame_index << '\n';
      return false;
    }
    expected_frame_colors.push_back(color_by_clip_id.value(active_clip_id));
  }

  const auto output_path = QDir(workspace_path).filePath(
      QStringLiteral("subframe-bounded-video-oracle.mp4"));
  QString error;
  const auto arguments =
      photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
          controller, output_path, QStringLiteral("bounded-h264-video"), 0.0, 1.0, &error);
  if (arguments.isEmpty() ||
      !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
          controller, arguments, &error)) {
    std::cerr << "The subframe bounded-video render failed: "
              << error.toStdString() << '\n';
    return false;
  }
  const auto actual_frame_count = count_video_frames(ffprobe, output_path);
  if (actual_frame_count != kExpectedFrameCount) {
    std::cerr << "Sixteen 0.0625-second clips should produce exactly 30 global-clock "
                 "frames, but bounded video produced "
              << actual_frame_count
              << "; raw subframe cuts may be rounding independently\n";
    return false;
  }
  const auto decoded = decode_all_video_frames_rgb24(ffmpeg, output_path);
  const auto frame_stride = static_cast<qint64>(frame_size.width()) *
                            frame_size.height() * 3;
  if (decoded.size() != frame_stride * kExpectedFrameCount) {
    std::cerr << "The subframe bounded-video output could not be decoded as 30 RGB frames\n";
    return false;
  }
  const auto center_pixel_offset =
      (static_cast<qint64>(frame_size.height() / 2) * frame_size.width() +
       frame_size.width() / 2) * 3;
  for (int frame_index = 0; frame_index < kExpectedFrameCount; ++frame_index) {
    const auto offset = static_cast<qsizetype>(
        static_cast<qint64>(frame_index) * frame_stride + center_pixel_offset);
    const QColor actual(
        static_cast<unsigned char>(decoded.at(offset)),
        static_cast<unsigned char>(decoded.at(offset + 1)),
        static_cast<unsigned char>(decoded.at(offset + 2)));
    const auto expected = expected_frame_colors.at(frame_index);
    if (qAbs(actual.red() - expected.red()) > 48 ||
        qAbs(actual.green() - expected.green()) > 48 ||
        qAbs(actual.blue() - expected.blue()) > 48) {
      std::cerr << "Bounded-video frame " << frame_index
                << " does not show the native preview's clip at global time "
                << static_cast<double>(frame_index) / kFramesPerSecond
                << "s; expected RGB(" << expected.red() << ',' << expected.green() << ','
                << expected.blue() << "), got RGB(" << actual.red() << ','
                << actual.green() << ',' << actual.blue() << ")\n";
      return false;
    }
  }
  return true;
}

bool run_bounded_h264_planner_checks() {
  using photo_editor::desktop::timeline_motion::bounded_h264::
      apply_ffmpeg_thread_limits;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      bounded_window_count;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      bounded_window_frame_limit;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      kBoundedWindowMaximumFrames;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      kCacheBudgetFraction;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      kMaximumRenderWindowSeconds;
  using photo_editor::desktop::timeline_motion::bounded_h264::plan_frame_ranges;
  QString error;
  if (bounded_window_frame_limit(30.0, kMaximumRenderWindowSeconds) != 16 ||
      bounded_window_frame_limit(60.0, kMaximumRenderWindowSeconds) != 16 ||
      bounded_window_frame_limit(4.0, kMaximumRenderWindowSeconds) != 16 ||
      bounded_window_frame_limit(3.0, kMaximumRenderWindowSeconds) != 12 ||
      bounded_window_frame_limit(0.24, kMaximumRenderWindowSeconds) != 1 ||
      bounded_window_frame_limit(0.0, kMaximumRenderWindowSeconds) != 0 ||
      bounded_window_frame_limit(30.0, 0.0) != 0 ||
      bounded_window_count(8940, kBoundedWindowMaximumFrames) != 559) {
    std::cerr << "The bounded H.264 policy should apply the 16-frame cap without exceeding its existing four-second limit\n";
    return false;
  }
  const auto ranges_cover_clock = [](
                                      const QVector<photo_editor::desktop::timeline_motion::bounded_h264::FrameRange>& ranges,
                                      qint64 total_frames,
                                      double fps,
                                      qint64 frame_cap) {
    auto next_frame = qint64{0};
    for (const auto& range : ranges) {
      const auto frame_count = range.end_frame - range.start_frame;
      const auto start_seconds = static_cast<double>(range.start_frame) / fps;
      const auto end_seconds = static_cast<double>(range.end_frame) / fps;
      if (range.start_frame != next_frame || frame_count <= 0 ||
          frame_count > frame_cap ||
          qAbs(start_seconds * fps - static_cast<double>(range.start_frame)) > 1.0e-8 ||
          qAbs(end_seconds * fps - static_cast<double>(range.end_frame)) > 1.0e-8) {
        return false;
      }
      next_frame = range.end_frame;
    }
    return next_frame == total_frames;
  };

  const QStringList uncapped_arguments{
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-filter_complex_script"), QStringLiteral("graph.ffgraph"),
      QStringLiteral("-f"), QStringLiteral("image2"),
      QStringLiteral("-framerate"), QStringLiteral("30"),
      QStringLiteral("-start_number"), QStringLiteral("0"),
      QStringLiteral("-i"), QStringLiteral("/cache/field_%06d.png"),
      QStringLiteral("-loop"), QStringLiteral("1"),
      QStringLiteral("-framerate"), QStringLiteral("30"),
      QStringLiteral("-i"), QStringLiteral("/cache/title.png"),
      QStringLiteral("-ss"), QStringLiteral("0"),
      QStringLiteral("-i"), QStringLiteral("/media/footage.mov"),
      QStringLiteral("-c:v"), QStringLiteral("libx264"),
      QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
      QStringLiteral("-y"), QStringLiteral("chunk.mp4"),
  };
  const auto capped_arguments = apply_ffmpeg_thread_limits(uncapped_arguments, &error);
  const auto count_option = [](const QStringList& values, const QString& option) {
    return std::count(values.cbegin(), values.cend(), option);
  };
  const auto first_input_index = capped_arguments.indexOf(QStringLiteral("-i"));
  const auto second_input_index = capped_arguments.indexOf(
      QStringLiteral("-i"), first_input_index + 1);
  const auto third_input_index = capped_arguments.indexOf(
      QStringLiteral("-i"), second_input_index + 1);
  const auto encoder_index = capped_arguments.indexOf(QStringLiteral("-c:v"));
  if (!error.isEmpty() || capped_arguments.isEmpty() ||
      capped_arguments.value(0) != QStringLiteral("-filter_complex_threads") ||
      capped_arguments.value(1) != QString::number(
          photo_editor::desktop::timeline_motion::bounded_h264::
              kBoundedFilterComplexThreads) ||
      first_input_index < 2 || capped_arguments.value(first_input_index - 2) !=
                                   QStringLiteral("-threads") ||
      capped_arguments.value(first_input_index - 1) != QString::number(
          photo_editor::desktop::timeline_motion::bounded_h264::
              kBoundedImageDecoderThreads) ||
      second_input_index < 2 || capped_arguments.value(second_input_index - 2) !=
                                    QStringLiteral("-threads") ||
      third_input_index < 0 || capped_arguments.value(third_input_index + 1) !=
                                   QStringLiteral("/media/footage.mov") ||
      encoder_index < 0 || capped_arguments.value(encoder_index + 2) !=
                               QStringLiteral("-threads") ||
      capped_arguments.value(encoder_index + 3) != QString::number(
          photo_editor::desktop::timeline_motion::bounded_h264::
              kBoundedH264EncoderThreads) ||
      count_option(capped_arguments, QStringLiteral("-filter_complex_threads")) != 1 ||
      count_option(capped_arguments, QStringLiteral("-threads")) != 3) {
    std::cerr << "Bounded H.264 thread limits must scope to the graph, still-image inputs, "
                 "and H.264 encoder while leaving video inputs unchanged: "
              << error.toStdString() << " argv="
              << capped_arguments.join(QLatin1Char(' ')).toStdString() << '\n';
    return false;
  }
  const auto idempotent_arguments = apply_ffmpeg_thread_limits(capped_arguments, &error);
  if (!error.isEmpty() || idempotent_arguments != capped_arguments) {
    std::cerr << "Applying bounded H.264 thread limits twice should preserve the exact argv: "
              << error.toStdString() << '\n';
    return false;
  }
  auto playback_first_arguments = uncapped_arguments;
  const auto playback_encoder_index = playback_first_arguments.indexOf(QStringLiteral("-c:v"));
  playback_first_arguments.insert(playback_encoder_index + 2, QStringLiteral("-threads"));
  playback_first_arguments.insert(playback_encoder_index + 3, QStringLiteral("1"));
  const auto playback_first_limited = apply_ffmpeg_thread_limits(
      playback_first_arguments, &error);
  const auto playback_first_limited_encoder =
      playback_first_limited.indexOf(QStringLiteral("-c:v"));
  if (!error.isEmpty() || playback_first_limited.isEmpty() ||
      playback_first_limited.value(playback_first_limited_encoder + 2) !=
          QStringLiteral("-threads") ||
      playback_first_limited.value(playback_first_limited_encoder + 3) !=
          QStringLiteral("1")) {
    std::cerr << "The bounded encoder cap should preserve the stricter playback-first policy: "
              << error.toStdString() << '\n';
    return false;
  }
  const auto malformed_arguments = apply_ffmpeg_thread_limits(
      {QStringLiteral("-i"), QStringLiteral("still.png"),
       QStringLiteral("-c:v"), QStringLiteral("libx264"),
       QStringLiteral("-y"), QStringLiteral("out.mp4")},
      &error);
  if (!malformed_arguments.isEmpty() || error.isEmpty()) {
    std::cerr << "Thread limiting should reject commands without a filter graph\n";
    return false;
  }

  const auto small_ranges = plan_frame_ranges(
      306, 30.0, QSize(128, 72),
      [](qint64, qint64) { return qint64{2}; }, &error);
  if (!error.isEmpty() || small_ranges.size() != 20 ||
      small_ranges.first().start_frame != 0 || small_ranges.first().end_frame != 16 ||
      small_ranges.at(1).start_frame != 16 || small_ranges.at(1).end_frame != 32 ||
      small_ranges.last().start_frame != 304 || small_ranges.last().end_frame != 306 ||
      !ranges_cover_clock(small_ranges, 306, 30.0,
                          bounded_window_frame_limit(30.0, kMaximumRenderWindowSeconds))) {
    std::cerr << "The 10.2-second planner should emit globally aligned windows of at most 16 frames: "
              << error.toStdString() << '\n';
    return false;
  }

  const auto four_k_ranges = plan_frame_ranges(
      306, 30.0, QSize(3840, 2160),
      [](qint64, qint64) { return qint64{2}; }, &error);
  const auto budget = static_cast<qint64>(
      photo_editor::desktop::timeline_motion::kNativeGeneratorFrameCacheBudgetBytes *
      kCacheBudgetFraction);
  if (!error.isEmpty() || four_k_ranges.isEmpty() ||
      !ranges_cover_clock(four_k_ranges, 306, 30.0,
                          bounded_window_frame_limit(30.0, kMaximumRenderWindowSeconds))) {
    std::cerr << "The 4K two-source planner should fit globally aligned frame windows: "
              << error.toStdString() << '\n';
    return false;
  }
  auto expected_start = qint64{0};
  for (const auto& range : four_k_ranges) {
    if (range.start_frame != expected_start || range.end_frame <= range.start_frame ||
        range.end_frame - range.start_frame > kBoundedWindowMaximumFrames ||
        range.estimated_source_bytes > budget) {
      std::cerr << "The 4K planner should stay contiguous, <=16 frames, and under 75% of cache budget\n";
      return false;
    }
    expected_start = range.end_frame;
  }
  if (expected_start != 306) {
    std::cerr << "The 4K planner should cover all 306 output frames exactly\n";
    return false;
  }
  const auto full_native_ranges = plan_frame_ranges(
      8940, 30.0, QSize(1920, 1080),
      [](qint64, qint64) { return qint64{2}; }, &error);
  if (!error.isEmpty() || full_native_ranges.size() != 559 ||
      !ranges_cover_clock(full_native_ranges, 8940, 30.0,
                          kBoundedWindowMaximumFrames) ||
      full_native_ranges.last().end_frame - full_native_ranges.last().start_frame != 12) {
    std::cerr << "The 298-second 30fps planner should cover all 8,940 global frames in 559 capped windows: "
              << error.toStdString() << '\n';
    return false;
  }
  constexpr double kFractionalFramesPerSecond = 30000.0 / 1001.0;
  const auto fractional_ranges = plan_frame_ranges(
      300, kFractionalFramesPerSecond, QSize(128, 72),
      [](qint64, qint64) { return qint64{2}; }, &error);
  if (!error.isEmpty() || fractional_ranges.size() != 19 ||
      !ranges_cover_clock(
          fractional_ranges, 300, kFractionalFramesPerSecond,
          bounded_window_frame_limit(kFractionalFramesPerSecond,
                                     kMaximumRenderWindowSeconds))) {
    std::cerr << "The fractional-fps planner should keep every chunk on the absolute frame clock: "
              << error.toStdString() << '\n';
    return false;
  }
  const auto impossible_ranges = plan_frame_ranges(
      30, 30.0, QSize(16384, 16384),
      [](qint64, qint64) { return qint64{2}; }, &error);
  if (!impossible_ranges.isEmpty() || error.isEmpty()) {
    std::cerr << "The planner should fail explicitly when even one procedural frame exceeds budget\n";
    return false;
  }
  return true;
}

}  // namespace

bool run_timeline_bounded_h264_export_workflow(const QString& workspace_path) {
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  if (ffmpeg.isEmpty() || ffprobe.isEmpty()) {
    std::cerr << "Bounded H.264 export workflow requires FFmpeg and ffprobe\n";
    return false;
  }
  if (!run_bounded_h264_planner_checks()) {
    return false;
  }
  if (!run_bounded_concat_frame_count_oracle(ffmpeg, ffprobe, workspace_path)) {
    return false;
  }
  if (!run_bounded_rational_frame_clock_oracle(ffmpeg, ffprobe, workspace_path)) {
    return false;
  }
  if (!run_guard_frame_removal_pixel_order_oracle(ffmpeg)) {
    return false;
  }
  if (!run_bounded_subframe_video_timing_oracle(
          ffmpeg, ffprobe, workspace_path)) {
    return false;
  }

  // Reproduce the native v4 desktop-source-image clip at small output size.
  // The compact fixture contains the source clip's dense frame, mask, and
  // opacity channels, without loading the 58 MiB saved-project snapshot.
  // Its 30 fps transform and sequence-mask keys distinguish a range-clock
  // problem from a mismatch in the image viewport or mask geometry.
  {
    constexpr double kZoomDurationSeconds = 12.0;
    constexpr int kZoomFramesPerSecond = 30;
    const QSize zoom_size(320, 180);
    const auto fixture_path = QDir(
        QFileInfo(QString::fromUtf8(__FILE__)).absolutePath())
                                  .filePath(QStringLiteral(
                                      "fixtures/timeline_native_zoom_scale_v4.json"));
    QFile fixture_file(fixture_path);
    if (!fixture_file.open(QIODevice::ReadOnly)) {
      std::cerr << "The exact native zoom keyframe fixture should be readable: "
                << fixture_path.toStdString() << '\n';
      return false;
    }
    QJsonParseError fixture_parse_error;
    const auto fixture = QJsonDocument::fromJson(
                             fixture_file.readAll(), &fixture_parse_error)
                             .object();
    if (fixture_parse_error.error != QJsonParseError::NoError ||
        fixture.value(QStringLiteral("frameMode")).toString() != QStringLiteral("fit") ||
        fixture.value(QStringLiteral("keyframes")).toArray().size() != 359) {
      std::cerr << "The native zoom fixture should retain all 359 exact frame, mask, and opacity keyframes\n";
      return false;
    }

    const auto zoom_source_path = QDir(workspace_path).filePath(
        QStringLiteral("bounded-zoom-clock-source.png"));
    constexpr int kSourceWidth = 2570;
    constexpr int kSourceHeight = 1504;
    QImage zoom_source(kSourceWidth, kSourceHeight, QImage::Format_RGB32);
    for (int y = 0; y < kSourceHeight; ++y) {
      auto* row = reinterpret_cast<QRgb*>(zoom_source.scanLine(y));
      for (int x = 0; x < kSourceWidth; ++x) {
        const auto shade = ((x / 64 + y / 64) & 1) == 0 ? 84 : 164;
        row[x] = qRgb(shade, shade, shade);
      }
    }
    if (!zoom_source.save(zoom_source_path)) {
      std::cerr << "The bounded scale-clock source should be writable\n";
      return false;
    }

    TimelineDocumentController zoom_controller;
    zoom_controller.createTimelineWithSettings({
        {QStringLiteral("timelineName"), QStringLiteral("Native dense zoom viewport oracle")},
        {QStringLiteral("width"), zoom_size.width()},
        {QStringLiteral("height"), zoom_size.height()},
        {QStringLiteral("frameRate"), static_cast<double>(kZoomFramesPerSecond)},
    });
    const auto zoom_track = zoom_controller.createTimelineTrackForAutomation(
        QStringLiteral("graphics"), QStringLiteral("Zoom photo"), {});
    const auto zoom_track_id = zoom_track.value(QStringLiteral("trackId")).toString();
    const auto zoom_clip = zoom_controller.createTimelineClipForAutomation(
        zoom_track_id,
        QStringLiteral("photo:native-dense-zoom-viewport"),
        zoom_source_path,
        QStringLiteral("Animated zoom photo"),
        kZoomDurationSeconds,
        0.0,
        kZoomDurationSeconds,
        0.0,
        zoom_source_path);
    const auto zoom_clip_id = zoom_clip.value(QStringLiteral("createdClipId")).toString();
    const auto frame_width = fixture.value(QStringLiteral("frameWidth")).toDouble();
    const auto frame_height = fixture.value(QStringLiteral("frameHeight")).toDouble();
    const auto frame_scale = fixture.value(QStringLiteral("frameScale")).toDouble();
    const auto frame_offset_x = fixture.value(QStringLiteral("frameOffsetX")).toDouble();
    const auto frame_offset_y = fixture.value(QStringLiteral("frameOffsetY")).toDouble();
    const auto frame_corner_radius = fixture.value(QStringLiteral("frameCornerRadius")).toDouble();
    const auto frame_mode = fixture.value(QStringLiteral("frameMode")).toString();
    const auto zoom_frame = zoom_controller.setTimelineClipFrameForAutomation(
        zoom_clip_id, frame_offset_x, frame_offset_y, frame_scale,
        0.0, 0.0, 0.0, 1.0, frame_mode, frame_width, frame_height, frame_corner_radius,
        0.0, 0.0, 1.0, 1.0);
    QMap<QString, QVariantList> points_by_parameter;
    for (const auto& keyframe_value : fixture.value(QStringLiteral("keyframes")).toArray()) {
      const auto keyframe = keyframe_value.toObject();
      points_by_parameter[keyframe.value(QStringLiteral("parameter")).toString()].push_back(
          QVariantMap{
              {QStringLiteral("localSeconds"), keyframe.value(QStringLiteral("seconds")).toDouble()},
              {QStringLiteral("value"), keyframe.value(QStringLiteral("value")).toDouble()},
              {QStringLiteral("interpolation"), keyframe.value(QStringLiteral("interpolation")).toString()},
          });
    }
    QVariantList channels;
    QVariantList transform_channels;
    for (auto iterator = points_by_parameter.cbegin();
         iterator != points_by_parameter.cend(); ++iterator) {
      const auto channel = QVariantMap{
          {QStringLiteral("parameter"), iterator.key()},
          {QStringLiteral("keyframes"), iterator.value()},
      };
      channels.push_back(channel);
      if (iterator.key() == QStringLiteral("frameScale") ||
          iterator.key() == QStringLiteral("frameOffsetX") ||
          iterator.key() == QStringLiteral("frameOffsetY")) {
        transform_channels.push_back(channel);
      }
    }
    const auto animation = zoom_controller.setTimelineClipAnimationForAutomation(
        zoom_clip_id, transform_channels);
    if (!zoom_track.value(QStringLiteral("ok")).toBool() ||
        zoom_track_id.isEmpty() || !zoom_clip.value(QStringLiteral("ok")).toBool() ||
        zoom_clip_id.isEmpty() || !zoom_frame.value(QStringLiteral("ok")).toBool() ||
        !animation.value(QStringLiteral("ok")).toBool() ||
        qAbs(zoom_controller.totalDurationSecondsValue() - kZoomDurationSeconds) > 1.0e-6) {
      std::cerr << "The native dense zoom fixture should admit the full-size fit photo and all transform channels: "
                << animation.value(QStringLiteral("error")).toString().toStdString() << '\n';
      return false;
    }

    const auto full_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-master.mp4"));
    const auto full_graph_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-master.ffgraph"));
    QString render_error;
    const auto full_arguments =
        photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
            zoom_controller, full_path, QStringLiteral("master"), -1.0, -1.0,
            &render_error, full_graph_path);
    if (full_arguments.isEmpty() ||
        !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
            zoom_controller, full_arguments, &render_error)) {
      QFile::remove(full_graph_path);
      std::cerr << "The full native dense zoom reference render should succeed: "
                << render_error.toStdString() << '\n';
      return false;
    }
    QFile::remove(full_graph_path);
    const auto bounded_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-bounded.mp4"));
    const auto bounded_graph_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-bounded.ffgraph"));
    render_error.clear();
    const auto bounded_arguments =
        photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
            zoom_controller, bounded_path, QStringLiteral("bounded-h264-video"),
            8.0, 10.2, &render_error, bounded_graph_path);
    if (bounded_arguments.isEmpty() ||
        !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
            zoom_controller, bounded_arguments, &render_error)) {
      QFile::remove(bounded_graph_path);
      std::cerr << "The [8, 10.2] native dense zoom render should succeed: "
                << render_error.toStdString() << '\n';
      return false;
    }
    QFile::remove(bounded_graph_path);

    const auto checker_bounds = [zoom_size](const QByteArray& rgb) {
      const auto expected_bytes = zoom_size.width() * zoom_size.height() * 3;
      if (rgb.size() != expected_bytes) {
        return QRect{};
      }
      auto left = zoom_size.width();
      auto top = zoom_size.height();
      auto right = -1;
      auto bottom = -1;
      for (int y = 0; y < zoom_size.height(); ++y) {
        for (int x = 0; x < zoom_size.width(); ++x) {
          const auto offset = (y * zoom_size.width() + x) * 3;
          const auto red = static_cast<unsigned char>(rgb.at(offset));
          const auto green = static_cast<unsigned char>(rgb.at(offset + 1));
          const auto blue = static_cast<unsigned char>(rgb.at(offset + 2));
          if (red > 55 && green > 55 && blue > 55) {
            left = qMin(left, x);
            top = qMin(top, y);
            right = qMax(right, x);
            bottom = qMax(bottom, y);
          }
        }
      }
      return right >= left && bottom >= top
                 ? QRect(QPoint(left, top), QPoint(right, bottom))
                 : QRect{};
    };
    const QVector<QPair<qint64, qint64>> sample_frames{
        {240, 0}, {270, 30}, {299, 59}, {305, 65}};
    const auto expected_parameter_at = [&fixture](
                                           const QString& parameter,
                                           double sequence_seconds,
                                           double base_value) {
      auto points = QVector<QPair<double, double>>{};
      for (const auto& value : fixture.value(QStringLiteral("keyframes")).toArray()) {
        const auto keyframe = value.toObject();
        if (keyframe.value(QStringLiteral("parameter")).toString() == parameter) {
          points.push_back({keyframe.value(QStringLiteral("seconds")).toDouble(),
                            keyframe.value(QStringLiteral("value")).toDouble()});
        }
      }
      if (points.isEmpty()) {
        return base_value;
      }
      if (sequence_seconds <= points.first().first) {
        return points.first().second;
      }
      for (qsizetype index = 0; index + 1 < points.size(); ++index) {
        const auto& left = points.at(index);
        const auto& right = points.at(index + 1);
        if (sequence_seconds <= right.first + 1.0e-9) {
          const auto fraction = (sequence_seconds - left.first) /
                                qMax(1.0e-9, right.first - left.first);
          return left.second + (right.second - left.second) * fraction;
        }
      }
      return points.last().second;
    };
    const QVector<double> sample_times{8.0, 9.0, 299.0 / 30.0, 305.0 / 30.0};
    QVector<QRect> measured_bounds;
    measured_bounds.reserve(sample_frames.size());
    for (qsizetype sample_index = 0; sample_index < sample_frames.size(); ++sample_index) {
      const auto [global_frame, bounded_frame] = sample_frames.at(sample_index);
      const auto sequence_seconds = sample_times.at(sample_index);
      zoom_controller.selectClip(-1);
      zoom_controller.seekSequenceSeconds(sequence_seconds);
      const auto native_frame = zoom_controller.programClip();
      const auto expected_scale = expected_parameter_at(
          QStringLiteral("frameScale"), sequence_seconds, frame_scale);
      const auto expected_offset_x = expected_parameter_at(
          QStringLiteral("frameOffsetX"), sequence_seconds, frame_offset_x);
      const auto expected_offset_y = expected_parameter_at(
          QStringLiteral("frameOffsetY"), sequence_seconds, frame_offset_y);
      const auto native_scale = native_frame.value(QStringLiteral("frameScale")).toDouble();
      const auto native_width = native_frame.value(QStringLiteral("frameWidth")).toDouble();
      const auto native_height = native_frame.value(QStringLiteral("frameHeight")).toDouble();
      const auto native_offset_x = native_frame.value(QStringLiteral("frameOffsetX")).toDouble();
      const auto native_offset_y = native_frame.value(QStringLiteral("frameOffsetY")).toDouble();
      if (!native_frame.contains(QStringLiteral("frameScale")) ||
          !native_frame.contains(QStringLiteral("frameWidth")) ||
          !native_frame.contains(QStringLiteral("frameHeight")) ||
          !native_frame.contains(QStringLiteral("frameOffsetX")) ||
          !native_frame.contains(QStringLiteral("frameOffsetY")) ||
          qAbs(native_scale - expected_scale) > 1.0e-4 ||
          qAbs(native_width - frame_width) > 1.0e-6 ||
          qAbs(native_height - frame_height) > 1.0e-6 ||
          qAbs(native_offset_x - expected_offset_x) > 1.0e-4 ||
          qAbs(native_offset_y - expected_offset_y) > 1.0e-4) {
        std::cerr << "Native Program transform differs at t=" << sequence_seconds
                  << " expectedScale=" << expected_scale << " projectedScale=" << native_scale
                  << " width=" << native_width << " height=" << native_height
                  << " expectedOffsetX=" << expected_offset_x << " projectedOffsetX=" << native_offset_x
                  << " expectedOffsetY=" << expected_offset_y << " projectedOffsetY=" << native_offset_y << '\n';
        return false;
      }
      const auto full_rgb = decode_video_frame_rgb24(
          ffmpeg, full_path, global_frame, zoom_size);
      const auto bounded_rgb = decode_video_frame_rgb24(
          ffmpeg, bounded_path, bounded_frame, zoom_size);
      const auto full_bounds = checker_bounds(full_rgb);
      const auto bounded_bounds = checker_bounds(bounded_rgb);
      if (full_bounds.isEmpty() || bounded_bounds.isEmpty() ||
          qAbs(full_bounds.x() - bounded_bounds.x()) > 1 ||
          qAbs(full_bounds.y() - bounded_bounds.y()) > 1 ||
          qAbs(full_bounds.width() - bounded_bounds.width()) > 1 ||
          qAbs(full_bounds.height() - bounded_bounds.height()) > 1) {
        std::cerr << "Bounded native zoom viewport diverged at t=" << sequence_seconds
                  << " globalFrame=" << global_frame << " expectedScale=" << expected_scale
                  << " projectedScale=" << native_scale << " offsetY=" << native_offset_y
                  << ": master=" << full_bounds.x() << ','
                  << full_bounds.y() << ',' << full_bounds.width() << ','
                  << full_bounds.height() << " bounded=" << bounded_bounds.x()
                  << ',' << bounded_bounds.y() << ',' << bounded_bounds.width()
                  << ',' << bounded_bounds.height() << '\n';
        return false;
      }
      measured_bounds.push_back(bounded_bounds);
    }
    if (measured_bounds.at(1).width() <= measured_bounds.at(0).width() + 5 ||
        measured_bounds.at(2).width() <= measured_bounds.at(1).width() + 2 ||
        measured_bounds.at(3).width() < measured_bounds.at(2).width()) {
      std::cerr << "The exact native frameScale channel should grow across global t8–10.2; widths="
                << measured_bounds.at(0).width() << ',' << measured_bounds.at(1).width()
                << ',' << measured_bounds.at(2).width() << ','
                << measured_bounds.at(3).width() << '\n';
      return false;
    }

    const auto source_aspect = static_cast<double>(kSourceWidth) / kSourceHeight;
    const auto fit_width = qMin(frame_width * zoom_size.width(),
                                frame_height * zoom_size.height() * source_aspect);
    const auto expected_t9_scale = expected_parameter_at(
        QStringLiteral("frameScale"), 9.0, frame_scale);
    const auto expected_t9_width = fit_width * expected_t9_scale;
    const auto expected_t9_left = (zoom_size.width() - expected_t9_width) / 2.0;
    const auto expected_t9_right = expected_t9_left + expected_t9_width - 1.0;
    const auto unmasked_t9_right = measured_bounds.at(1).x() + measured_bounds.at(1).width() - 1;
    if (qAbs(measured_bounds.at(1).width() - expected_t9_width) > 3.0 ||
        qAbs(measured_bounds.at(1).x() - expected_t9_left) > 3.0 ||
        qAbs(unmasked_t9_right - expected_t9_right) > 3.0) {
      std::cerr << "The unmasked native t=9 fit image should match its evaluated scale: expected width="
                << expected_t9_width << " left/right=" << expected_t9_left << '/' << expected_t9_right
                << " measured=" << measured_bounds.at(1).x() << '/' << unmasked_t9_right
                << " width=" << measured_bounds.at(1).width()
                << " scale=" << expected_t9_scale << '\n';
      return false;
    }

    // Reproduce the exact collapsed-base ellipse, animated edges, corner
    // radius, and opacity. At t=9 the large ellipse contains the image card,
    // so its visible bounds should match both the unmasked control and fit math.
    const auto base_mask = fixture.value(QStringLiteral("clipMask")).toObject();
    const auto mask_result = zoom_controller.setTimelineClipMaskForAutomation(
        zoom_clip_id,
        QVariantMap{
            {QStringLiteral("shape"), base_mask.value(QStringLiteral("shape")).toString()},
            {QStringLiteral("left"), base_mask.value(QStringLiteral("left")).toDouble()},
            {QStringLiteral("top"), base_mask.value(QStringLiteral("top")).toDouble()},
            {QStringLiteral("right"), base_mask.value(QStringLiteral("right")).toDouble()},
            {QStringLiteral("bottom"), base_mask.value(QStringLiteral("bottom")).toDouble()},
            {QStringLiteral("cornerRadius"), base_mask.value(QStringLiteral("cornerRadius")).toDouble()},
        });
    const auto masked_base_frame = zoom_controller.setTimelineClipFrameForAutomation(
        zoom_clip_id, frame_offset_x, frame_offset_y, frame_scale,
        0.0, 0.0, 0.0, fixture.value(QStringLiteral("opacity")).toDouble(),
        frame_mode, frame_width, frame_height, frame_corner_radius,
        0.0, 0.0, 1.0, 1.0);
    const auto full_animation = zoom_controller.setTimelineClipAnimationForAutomation(
        zoom_clip_id, channels);
    if (!mask_result.value(QStringLiteral("ok")).toBool() ||
        !masked_base_frame.value(QStringLiteral("ok")).toBool() ||
        !full_animation.value(QStringLiteral("ok")).toBool()) {
      std::cerr << "The exact native animated ellipse mask, opacity, and transform channels should be admitted: mask="
                << mask_result.value(QStringLiteral("error")).toString().toStdString()
                << " frame=" << masked_base_frame.value(QStringLiteral("error")).toString().toStdString()
                << " animation=" << full_animation.value(QStringLiteral("error")).toString().toStdString()
                << '\n';
      return false;
    }

    const auto masked_full_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-masked-master.mp4"));
    const auto masked_full_graph_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-masked-master.ffgraph"));
    render_error.clear();
    const auto masked_full_arguments =
        photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
            zoom_controller, masked_full_path, QStringLiteral("master"), -1.0, -1.0,
            &render_error, masked_full_graph_path);
    if (masked_full_arguments.isEmpty() ||
        !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
            zoom_controller, masked_full_arguments, &render_error)) {
      QFile::remove(masked_full_graph_path);
      std::cerr << "The full native masked zoom reference render should succeed: "
                << render_error.toStdString() << '\n';
      return false;
    }
    QFile::remove(masked_full_graph_path);
    const auto masked_bounded_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-masked-bounded.mp4"));
    const auto masked_bounded_graph_path = QDir(workspace_path).filePath(
        QStringLiteral("native-dense-zoom-masked-bounded.ffgraph"));
    render_error.clear();
    const auto masked_bounded_arguments =
        photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
            zoom_controller, masked_bounded_path, QStringLiteral("bounded-h264-video"),
            8.0, 10.2, &render_error, masked_bounded_graph_path);
    if (masked_bounded_arguments.isEmpty() ||
        !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
            zoom_controller, masked_bounded_arguments, &render_error)) {
      QFile::remove(masked_bounded_graph_path);
      std::cerr << "The [8, 10.2] native masked zoom render should succeed: "
                << render_error.toStdString() << '\n';
      return false;
    }
    QFile::remove(masked_bounded_graph_path);

    QVector<QRect> masked_master_bounds;
    QVector<QRect> masked_window_bounds;
    masked_master_bounds.reserve(sample_frames.size());
    masked_window_bounds.reserve(sample_frames.size());
    for (qsizetype sample_index = 0; sample_index < sample_frames.size(); ++sample_index) {
      const auto [global_frame, bounded_frame] = sample_frames.at(sample_index);
      const auto sequence_seconds = sample_times.at(sample_index);
      zoom_controller.seekSequenceSeconds(sequence_seconds);
      const auto masked_native_frame = zoom_controller.programClip();
      const auto masked_projected_mask = masked_native_frame.value(QStringLiteral("clipMask")).toMap();
      const auto expected_scale = expected_parameter_at(
          QStringLiteral("frameScale"), sequence_seconds, frame_scale);
      const auto expected_offset_x = expected_parameter_at(
          QStringLiteral("frameOffsetX"), sequence_seconds, frame_offset_x);
      const auto expected_offset_y = expected_parameter_at(
          QStringLiteral("frameOffsetY"), sequence_seconds, frame_offset_y);
      const auto expected_opacity = expected_parameter_at(
          QStringLiteral("opacity"), sequence_seconds,
          fixture.value(QStringLiteral("opacity")).toDouble());
      const auto mask_edge_matches = [&expected_parameter_at, &masked_projected_mask,
                                      sequence_seconds](const QString& edge) {
        return qAbs(masked_projected_mask.value(edge).toDouble() -
                    expected_parameter_at(QStringLiteral("mask:") + edge,
                                          sequence_seconds, 0.0)) <= 1.0e-4;
      };
      const auto full_rgb = decode_video_frame_rgb24(
          ffmpeg, masked_full_path, global_frame, zoom_size);
      const auto bounded_rgb = decode_video_frame_rgb24(
          ffmpeg, masked_bounded_path, bounded_frame, zoom_size);
      const auto full_bounds = checker_bounds(full_rgb);
      const auto bounded_bounds = checker_bounds(bounded_rgb);
      const auto visible = expected_opacity > 0.65;
      if (qAbs(masked_native_frame.value(QStringLiteral("frameScale")).toDouble() -
               expected_scale) > 1.0e-4 ||
          qAbs(masked_native_frame.value(QStringLiteral("frameOffsetX")).toDouble() -
               expected_offset_x) > 1.0e-4 ||
          qAbs(masked_native_frame.value(QStringLiteral("frameOffsetY")).toDouble() -
               expected_offset_y) > 1.0e-4 ||
          qAbs(masked_native_frame.value(QStringLiteral("opacity")).toDouble() -
               expected_opacity) > 1.0e-4 ||
          qAbs(masked_native_frame.value(QStringLiteral("frameCornerRadius")).toDouble() -
               frame_corner_radius) > 1.0e-6 ||
          !masked_projected_mask.value(QStringLiteral("enabled")).toBool() ||
          !mask_edge_matches(QStringLiteral("left")) ||
          !mask_edge_matches(QStringLiteral("top")) ||
          !mask_edge_matches(QStringLiteral("right")) ||
          !mask_edge_matches(QStringLiteral("bottom")) ||
          masked_native_frame.value(QStringLiteral("sourceWidth")).toInt() !=
              fixture.value(QStringLiteral("sourceWidth")).toInt() ||
          masked_native_frame.value(QStringLiteral("sourceHeight")).toInt() !=
              fixture.value(QStringLiteral("sourceHeight")).toInt() ||
          full_rgb.isEmpty() || bounded_rgb.isEmpty() ||
          visible != !full_bounds.isEmpty() || visible != !bounded_bounds.isEmpty() ||
          (visible && (qAbs(full_bounds.x() - bounded_bounds.x()) > 1 ||
                       qAbs(full_bounds.y() - bounded_bounds.y()) > 1 ||
                       qAbs(full_bounds.width() - bounded_bounds.width()) > 1 ||
                       qAbs(full_bounds.height() - bounded_bounds.height()) > 1))) {
        std::cerr << "Native masked transform, alpha, or sequence mask differs at t="
                  << sequence_seconds << " scale="
                  << masked_native_frame.value(QStringLiteral("frameScale")).toDouble()
                  << '/' << expected_scale << " opacity="
                  << masked_native_frame.value(QStringLiteral("opacity")).toDouble()
                  << '/' << expected_opacity << " mask="
                  << masked_projected_mask.value(QStringLiteral("left")).toDouble() << ','
                  << masked_projected_mask.value(QStringLiteral("top")).toDouble() << ','
                  << masked_projected_mask.value(QStringLiteral("right")).toDouble() << ','
                  << masked_projected_mask.value(QStringLiteral("bottom")).toDouble()
                  << " master=" << full_bounds.x() << ',' << full_bounds.width()
                  << " bounded=" << bounded_bounds.x() << ',' << bounded_bounds.width() << '\n';
        return false;
      }
      masked_master_bounds.push_back(full_bounds);
      masked_window_bounds.push_back(bounded_bounds);
    }

    const auto masked_t9_bounds = masked_master_bounds.at(1);
    const auto masked_bounded_t9_bounds = masked_window_bounds.at(1);
    if (masked_t9_bounds.isEmpty() || masked_bounded_t9_bounds.isEmpty() ||
        qAbs(masked_t9_bounds.width() - measured_bounds.at(1).width()) > 2 ||
        qAbs(masked_t9_bounds.x() - measured_bounds.at(1).x()) > 2 ||
        qAbs(masked_bounded_t9_bounds.width() - masked_t9_bounds.width()) > 1 ||
        qAbs(masked_bounded_t9_bounds.x() - masked_t9_bounds.x()) > 1 ||
        qAbs(masked_t9_bounds.width() - expected_t9_width) > 3.0 ||
        qAbs(masked_t9_bounds.x() - expected_t9_left) > 3.0 ||
        qAbs(masked_t9_bounds.x() + masked_t9_bounds.width() - 1 - expected_t9_right) > 3.0) {
      std::cerr << "The exact native t=9 masked source should retain its full animated fit bounds: expected="
                << expected_t9_left << '/' << expected_t9_right << " unmasked=" << measured_bounds.at(1).x()
                << ',' << measured_bounds.at(1).width() << " masked-master="
                << masked_t9_bounds.x() << '/'
                << masked_t9_bounds.x() + masked_t9_bounds.width() - 1 << " width=" << masked_t9_bounds.width()
                << " masked-bounded=" << masked_bounded_t9_bounds.x() << ','
                << masked_bounded_t9_bounds.width() << " scale=" << expected_t9_scale
                << " opacity=" << expected_parameter_at(
                       QStringLiteral("opacity"), 9.0,
                       fixture.value(QStringLiteral("opacity")).toDouble())
                << " radius=" << frame_corner_radius
                << " source=" << fixture.value(QStringLiteral("sourceWidth")).toInt()
                << 'x' << fixture.value(QStringLiteral("sourceHeight")).toInt()
                << " mask=" << expected_parameter_at(QStringLiteral("mask:left"), 9.0, 0.0)
                << ',' << expected_parameter_at(QStringLiteral("mask:top"), 9.0, 0.0)
                << ',' << expected_parameter_at(QStringLiteral("mask:right"), 9.0, 0.0)
                << ',' << expected_parameter_at(QStringLiteral("mask:bottom"), 9.0, 0.0) << '\n';
      return false;
    }
  }

  constexpr double kDurationSeconds = 10.2;
  constexpr int kFramesPerSecond = 30;
  const QSize frame_size(128, 72);
  const auto source_path = QDir(workspace_path).filePath(QStringLiteral("bounded-source.png"));
  QImage source(frame_size, QImage::Format_RGB32);
  source.fill(QColor(30, 80, 130));
  if (!source.save(source_path)) {
    std::cerr << "The bounded H.264 source still should be writable\n";
    return false;
  }
  const auto tone_path = QDir(workspace_path).filePath(QStringLiteral("bounded-tone.wav"));
  if (!create_sine_wave_file(ffmpeg, tone_path, 440, kDurationSeconds)) {
    std::cerr << "The bounded H.264 continuous audio fixture should be writable\n";
    return false;
  }

  TimelineDocumentController controller;
  controller.createTimelineWithSettings({
      {QStringLiteral("timelineName"), QStringLiteral("Bounded H.264 seams")},
      {QStringLiteral("width"), frame_size.width()},
      {QStringLiteral("height"), frame_size.height()},
      {QStringLiteral("frameRate"), static_cast<double>(kFramesPerSecond)},
  });
  controller.addClipFromGallery(
      QStringLiteral("photo:bounded-h264-background"), source_path,
      QStringLiteral("Still background"), kDurationSeconds, QString{});
  controller.addMusicTrack();
  controller.addClipFromGallery(
      QStringLiteral("audio:bounded-h264-tone"), tone_path,
      QStringLiteral("Continuous tone"), kDurationSeconds, QString{});

  const QVariantMap vector_path{
      {QStringLiteral("viewBoxWidth"), static_cast<double>(frame_size.width())},
      {QStringLiteral("viewBoxHeight"), static_cast<double>(frame_size.height())},
      {QStringLiteral("commands"), QVariantList{
          QVariantMap{{QStringLiteral("verb"), QStringLiteral("M")},
                      {QStringLiteral("points"), QVariantList{8.0, 36.0}}},
          QVariantMap{{QStringLiteral("verb"), QStringLiteral("L")},
                      {QStringLiteral("points"), QVariantList{120.0, 36.0}}},
      }},
      {QStringLiteral("stroke"), QStringLiteral("#ffffff")},
      {QStringLiteral("strokeWidth"), 3.0},
      {QStringLiteral("fill"), QStringLiteral("#00000000")},
  };
  const auto create_generator = [&controller](
                                   const QString& kind,
                                   const QVariantMap& parameters) {
    const auto track = controller.createTimelineTrackForAutomation(
        QStringLiteral("graphics"), kind, {});
    if (!track.value(QStringLiteral("ok")).toBool()) {
      return QVariantMap{{QStringLiteral("ok"), false},
                         {QStringLiteral("error"), QStringLiteral("track creation failed")}};
    }
    return controller.createTimelineGeneratorForAutomation(
        track.value(QStringLiteral("trackId")).toString(), kind, 0.0,
        kDurationSeconds, parameters);
  };
  const auto light = create_generator(
      QStringLiteral("desktop-light"),
      {{QStringLiteral("timeOffsetSeconds"), 0.0},
       {QStringLiteral("timeScale"), 1.0},
       {QStringLiteral("gather"), 0.22}});
  const auto vector = create_generator(
      QStringLiteral("vector-path"), {{QStringLiteral("vectorPath"), vector_path}});
  const auto light_clip_id = light.value(QStringLiteral("clip")).toMap()
                                 .value(QStringLiteral("clipId")).toString();
  const auto vector_clip_id = vector.value(QStringLiteral("clip")).toMap()
                                  .value(QStringLiteral("clipId")).toString();
  const QVariantList light_keyframes{
      QVariantMap{{QStringLiteral("localSeconds"), 0.0},
                  {QStringLiteral("value"), 0.35},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 3.9666666667},
                  {QStringLiteral("value"), 0.35},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 4.0333333333},
                  {QStringLiteral("value"), 0.82},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 7.9666666667},
                  {QStringLiteral("value"), 0.82},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 8.0333333333},
                  {QStringLiteral("value"), 0.35},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
      QVariantMap{{QStringLiteral("localSeconds"), 10.1666666667},
                  {QStringLiteral("value"), 0.35},
                  {QStringLiteral("interpolation"), QStringLiteral("linear")}},
  };
  const auto light_animation = controller.setTimelineClipAnimationForAutomation(
      light_clip_id,
      QVariantList{QVariantMap{
          {QStringLiteral("parameter"), QStringLiteral("opacity")},
          {QStringLiteral("keyframes"), light_keyframes},
      }});
  if (!light.value(QStringLiteral("ok")).toBool() ||
      !vector.value(QStringLiteral("ok")).toBool() ||
      light_clip_id.isEmpty() || vector_clip_id.isEmpty() ||
      !light_animation.value(QStringLiteral("ok")).toBool() ||
      qAbs(controller.totalDurationSecondsValue() - kDurationSeconds) > 0.001) {
    std::cerr << "The bounded H.264 fixture should contain the still, audio, and two animated native sources: "
              << light.value(QStringLiteral("error")).toString().toStdString() << "; "
              << vector.value(QStringLiteral("error")).toString().toStdString() << "; "
              << light_animation.value(QStringLiteral("error")).toString().toStdString() << '\n';
    return false;
  }

  const auto first_range_requests =
      photo_editor::desktop::TimelineHistoryTestAccess::nativeGeneratorFrameSequenceRequestsForRange(
          controller, 0.0, 4.0, frame_size);
  const auto second_range_requests =
      photo_editor::desktop::TimelineHistoryTestAccess::nativeGeneratorFrameSequenceRequestsForRange(
          controller, 4.0, 8.0, frame_size);
  if (first_range_requests.size() != 2 || second_range_requests.size() != 2) {
    std::cerr << "The bounded source cache should include both light and vector-path sources in each window\n";
    return false;
  }
  for (const auto& request : first_range_requests) {
    if (request.samples.size() > 120 || request.samples.isEmpty() ||
        qAbs(request.samples.first().sequence_seconds) > 1.0e-6) {
      std::cerr << "The first cached source range should contain only its frame window on the absolute sequence clock\n";
      return false;
    }
  }
  for (const auto& request : second_range_requests) {
    if (request.samples.size() > 120 || request.samples.isEmpty() ||
        qAbs(request.samples.first().sequence_seconds - 4.0) > 1.0e-6) {
      std::cerr << "The second cached source range should begin at sequence frame 120\n";
      return false;
    }
  }

  const auto reference_path = QDir(workspace_path).filePath(QStringLiteral("bounded-reference.mp4"));
  QString reference_error;
  const auto reference_arguments =
      photo_editor::desktop::TimelineHistoryTestAccess::renderArguments(
          controller, reference_path, QStringLiteral("master"), -1.0, -1.0,
          &reference_error);
  QString reference_graph;
  if (reference_arguments.isEmpty() ||
      !(has_explicit_limited_range_conversion(reference_arguments, &reference_graph) ||
        has_bounded_sdr_limited_range_conversion(reference_arguments, &reference_graph))) {
    std::cerr << "The SDR YUV reference graph should convert its full-range RGBA composite to limited BT.709 before tagging it: graphChars="
              << reference_graph.size() << '\n';
    return false;
  }
  if (
      !photo_editor::desktop::TimelineHistoryTestAccess::runTimelineFfmpeg(
          controller, reference_arguments, &reference_error)) {
    std::cerr << "The whole-timeline reference render should succeed: "
              << reference_error.toStdString() << '\n';
    return false;
  }

  const auto bounded_path = QDir(workspace_path).filePath(QStringLiteral("bounded-export.mp4"));
  if (!controller.renderCurrentTimeline(bounded_path) ||
      !QFileInfo::exists(bounded_path) || QFileInfo(bounded_path).size() <= 0) {
    std::cerr << "The bounded H.264 orchestration should export: "
              << controller.status().toStdString() << '\n';
    return false;
  }
  if (count_video_frames(ffprobe, bounded_path) != 306) {
    std::cerr << "The 10.2-second bounded export should contain exactly 306 frames\n";
    return false;
  }
  const auto audio_streams = process_stdout(
      ffprobe,
      {
          QStringLiteral("-v"), QStringLiteral("error"),
          QStringLiteral("-select_streams"), QStringLiteral("a"),
          QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name,sample_rate,channels"),
          QStringLiteral("-of"), QStringLiteral("csv=p=0"), bounded_path,
      })
                                 .trimmed()
                                 .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  if (audio_streams.size() != 1 ||
      !audio_streams.first().contains(QStringLiteral("aac")) ||
      !audio_streams.first().contains(QStringLiteral("48000")) ||
      !audio_streams.first().endsWith(QStringLiteral("2"))) {
    std::cerr << "The final mux should contain one 48 kHz stereo AAC stream\n";
    return false;
  }

  const QVector<double> audio_probe_times{
      3.90, 3.98, 4.02, 4.10, 7.90, 7.98, 8.02, 8.10, 9.80};
  for (const auto seconds : audio_probe_times) {
    if (decoded_tone_window_db(ffmpeg, bounded_path, 440, seconds, 0.06) < -48.0) {
      std::cerr << "The single continuous AAC stem should remain audible around time "
                << seconds << " seconds\n";
      return false;
    }
  }
  if (!assert_seam_frames_close(
          ffmpeg, reference_path, bounded_path, frame_size,
          {119, 120, 121, 239, 240, 241}) ||
      photo_editor::desktop::timeline_motion::native_generator_frame_sequence_cache_busy()) {
    std::cerr << "The chunk seams should match global-time rendering and release all source cache leases\n";
    return false;
  }

  const auto cache_root = first_range_requests.first().cache_root;
  QDirIterator cache_iterator(cache_root, QDir::Files, QDirIterator::Subdirectories);
  qint64 cache_bytes = 0;
  while (cache_iterator.hasNext()) {
    cache_iterator.next();
    cache_bytes += cache_iterator.fileInfo().size();
  }
  if (cache_bytes > photo_editor::desktop::timeline_motion::
                        kNativeGeneratorFrameCacheBudgetBytes) {
    std::cerr << "The bounded source cache should stay within its 2 GiB cap\n";
    return false;
  }
  std::cout << "Bounded H.264 export preserved 306 frames, global-time seam frames, "
               "continuous AAC, and released procedural cache leases\n";
  return true;
}
