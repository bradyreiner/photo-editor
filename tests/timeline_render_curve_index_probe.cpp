#include "timeline_render_curve_index.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <iostream>

namespace {

using photo_editor::desktop::timeline_motion::render_curve::ChannelIndex;
using photo_editor::desktop::timeline_motion::render_curve::Interpolation;
using photo_editor::desktop::timeline_motion::render_curve::Keyframe;
using photo_editor::desktop::timeline_motion::render_curve::Window;

constexpr double kFps = 30.0;
constexpr qint64 kTotalFrames = 8940;
constexpr qint64 kChunkFrames = 16;

bool check(bool condition, const QString& message) {
  if (!condition) {
    std::cerr << message.toStdString() << '\n';
  }
  return condition;
}

QString number(double value) {
  return QString::number(value, 'f', 8);
}

QVector<Keyframe> sample_curve() {
  return {
      Keyframe{0.0, 0.08, Interpolation::Linear},
      Keyframe{0.4, 0.92, Interpolation::Hold},
      Keyframe{0.8, 0.18, Interpolation::EaseIn},
      Keyframe{1.2, 0.81, Interpolation::EaseOut},
      Keyframe{1.6, 0.13, Interpolation::EaseInOut},
      Keyframe{2.0, 0.76, Interpolation::Bezier, true, 0.10, 0.18},
      Keyframe{2.4, 0.28, Interpolation::Linear, false, 0.0, 0.0,
               true, -0.12, -0.10},
      Keyframe{2.8, 0.88, Interpolation::Linear},
      Keyframe{3.2, 0.31, Interpolation::Bezier},
      Keyframe{3.6, 0.69, Interpolation::EaseInOut},
      Keyframe{4.0, 0.44, Interpolation::Linear},
  };
}

QByteArray render_ffmpeg(const QString& graph, qint64 expected_frames,
                         QString* error_message) {
  QTemporaryDir graph_directory;
  if (!graph_directory.isValid()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The FFmpeg graph fixture directory could not be created.");
    }
    return {};
  }
  const auto graph_path = graph_directory.filePath(QStringLiteral("probe.ffgraph"));
  QFile graph_file(graph_path);
  const auto graph_bytes = graph.toUtf8();
  if (!graph_file.open(QIODevice::WriteOnly) ||
      graph_file.write(graph_bytes) != graph_bytes.size()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The FFmpeg graph fixture could not be written.");
    }
    return {};
  }
  graph_file.close();

  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  if (ffmpeg.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("FFmpeg is unavailable for the pixel parity probe.");
    }
    return {};
  }
  QProcess process;
  process.start(ffmpeg, {
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-filter_complex_threads"), QStringLiteral("1"),
      QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
      QStringLiteral("color=c=black:s=2x2:r=30:d=%1")
          .arg(number(static_cast<double>(expected_frames) / kFps)),
      QStringLiteral("-filter_complex_script"), graph_path,
      QStringLiteral("-map"), QStringLiteral("[outv]"),
      QStringLiteral("-frames:v"), QString::number(expected_frames),
      QStringLiteral("-pix_fmt"), QStringLiteral("gray"),
      QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-"),
  });
  if (!process.waitForStarted(5000)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("FFmpeg could not start for the expression parity probe: %1")
                           .arg(process.errorString());
    }
    return {};
  }
  if (!process.waitForFinished(120000)) {
    process.kill();
    static_cast<void>(process.waitForFinished(1000));
    if (error_message != nullptr) {
      *error_message = QStringLiteral("FFmpeg timed out during the expression parity probe.");
    }
    return {};
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    if (error_message != nullptr) {
      *error_message = QString::fromUtf8(process.readAllStandardError());
    }
    return {};
  }
  return process.readAllStandardOutput();
}

bool test_validation_and_legacy_expression_shape() {
  QString error;
  auto simple = ChannelIndex::create({
      Keyframe{0.0, 0.0, Interpolation::Linear},
      Keyframe{1.0, 1.0, Interpolation::Linear},
  }, 0.5, &error);
  if (!check(simple.has_value(), QStringLiteral("A valid linear channel should index."))) {
    return false;
  }
  const auto expression = simple->ffmpegExpression(QStringLiteral("T"), &error);
  const auto legacy_shape = QStringLiteral(
      "if(lte(T,0.00000000),0.00000000,"
      "if(gte(T,1.00000000),1.00000000,"
      "(0.00000000+(1.00000000-0.00000000)*"
      "((T-0.00000000)/1.00000000))))");
  if (!check(expression == legacy_shape,
             QStringLiteral("The full helper expression must retain the current FFmpeg tree shape."))) {
    return false;
  }

  const auto constant = ChannelIndex::create({}, 0.375, &error);
  if (!check(constant.has_value() &&
                 constant->ffmpegExpression(QStringLiteral("T"), &error) ==
                     QStringLiteral("0.37500000"),
             QStringLiteral("An unanimated channel should emit its base value."))) {
    return false;
  }
  if (!check(!ChannelIndex::create({
                  Keyframe{0.0, 0.0, Interpolation::Linear},
                  Keyframe{0.0, 1.0, Interpolation::Linear},
              }, 0.0, &error)
                  .has_value(),
             QStringLiteral("Duplicate keyframe times must retain the legacy fallback."))) {
    return false;
  }
  if (!check(!photo_editor::desktop::timeline_motion::render_curve::
                  interpolationFromString(QStringLiteral("unsupported"))
                  .has_value(),
             QStringLiteral("Unknown interpolation names must not be normalized to linear."))) {
    return false;
  }
  return true;
}

bool test_exact_range_evaluation() {
  auto points = sample_curve();
  std::reverse(points.begin(), points.end());
  QString error;
  const auto index = ChannelIndex::create(std::move(points), -0.25, &error);
  if (!check(index.has_value(), QStringLiteral("The mixed interpolation fixture should index."))) {
    return false;
  }
  if (!check(index->evaluate(-1.0) == index->keyframes().first().value &&
                 index->evaluate(5.0) == index->keyframes().last().value,
             QStringLiteral("Full-curve evaluation must retain outside endpoint semantics."))) {
    return false;
  }
  auto serialized = qint64{0};
  auto max_window_size = qsizetype{0};
  for (qint64 first = 0; first < kTotalFrames; first += kChunkFrames) {
    const auto end = qMin(kTotalFrames, first + kChunkFrames);
    const auto start_seconds = static_cast<double>(first) / kFps;
    const auto end_seconds = static_cast<double>(end) / kFps;
    const auto window = index->windowForRange(start_seconds, end_seconds, &error);
    if (!check(window.has_value(), QStringLiteral("A frame-aligned curve window should select."))) {
      return false;
    }
    const auto count = window->end - window->first;
    serialized += count;
    max_window_size = qMax(max_window_size, count);
    for (auto frame = first; frame < end; ++frame) {
      const auto time = static_cast<double>(frame) / kFps;
      const auto full_value = index->evaluate(time);
      const auto sliced_value = index->evaluate(*window, time);
      if (!check(std::isfinite(full_value) && std::isfinite(sliced_value) &&
                     qAbs(full_value - sliced_value) <= 1.0e-12,
                 QStringLiteral("A range slice changed interpolation at global frame %1.")
                     .arg(frame))) {
        return false;
      }
    }
  }
  return check(serialized <= index->keyframeCount() + 2 *
                                   ((kTotalFrames + kChunkFrames - 1) / kChunkFrames) &&
                   max_window_size <= index->keyframeCount(),
               QStringLiteral("Range windows must process each channel key once plus boundary anchors."));
}

bool test_ffmpeg_pixels_match_across_windows() {
  QString error;
  const auto index = ChannelIndex::create(sample_curve(), 0.5, &error);
  if (!check(index.has_value(), QStringLiteral("The FFmpeg parity fixture should index."))) {
    return false;
  }
  constexpr qint64 total_frames = 120;
  constexpr qint64 frames_per_chunk = 16;
  const auto chunk_count =
      (total_frames + frames_per_chunk - 1) / frames_per_chunk;
  const auto build_graph = [&index, &error, total_frames](bool sliced) {
    QStringList parts;
    QStringList split_labels;
    QStringList concat_labels;
    for (qint64 chunk = 0; chunk < chunk_count; ++chunk) {
      split_labels.push_back(QStringLiteral("[s%1]").arg(chunk));
      concat_labels.push_back(QStringLiteral("[v%1]").arg(chunk));
    }
    parts.push_back(QStringLiteral("[0:v]format=gray,split=%1%2")
                        .arg(chunk_count)
                        .arg(split_labels.join(QString{})));
    for (qint64 chunk = 0; chunk < chunk_count; ++chunk) {
      const auto first_frame = chunk * frames_per_chunk;
      const auto end_frame = qMin(total_frames, first_frame + frames_per_chunk);
      const auto start_seconds = static_cast<double>(first_frame) / kFps;
      const auto end_seconds = static_cast<double>(end_frame) / kFps;
      const auto time_expression =
          QStringLiteral("(T+%1)").arg(number(start_seconds));
      const auto expression = sliced
                                  ? index->ffmpegExpressionForRange(
                                        start_seconds, end_seconds,
                                        time_expression, &error)
                                  : index->ffmpegExpression(time_expression, &error);
      if (expression.isEmpty()) {
        return QString{};
      }
      parts.push_back(QStringLiteral(
          "[s%1]trim=start_frame=%2:end_frame=%3,setpts=PTS-STARTPTS,"
          "geq=lum='clip((%4)*255,0,255)'[v%1]")
                          .arg(chunk)
                          .arg(first_frame)
                          .arg(end_frame)
                          .arg(expression));
    }
    parts.push_back(QStringLiteral("%1concat=n=%2:v=1:a=0[outv]")
                        .arg(concat_labels.join(QString{}))
                        .arg(chunk_count));
    return parts.join(QLatin1Char(';'));
  };
  const auto full_graph = build_graph(false);
  if (!check(!full_graph.isEmpty(), QStringLiteral("Full-curve expressions should build: %1")
                                        .arg(error))) {
    return false;
  }
  const auto full_pixels = render_ffmpeg(full_graph, total_frames, &error);
  if (!check(full_pixels.size() == total_frames * 4,
             QStringLiteral("The full-curve FFmpeg render should produce 120 2x2 frames: %1")
                 .arg(error))) {
    return false;
  }
  const auto sliced_graph = build_graph(true);
  if (!check(!sliced_graph.isEmpty(), QStringLiteral("Sliced expressions should build: %1")
                                        .arg(error))) {
    return false;
  }
  const auto sliced_pixels = render_ffmpeg(sliced_graph, total_frames, &error);
  if (!check(sliced_pixels.size() == full_pixels.size(),
             QStringLiteral("The sliced FFmpeg render should retain every frame: %1")
                 .arg(error))) {
    return false;
  }
  if (sliced_pixels != full_pixels) {
    for (qsizetype index = 0; index < full_pixels.size(); ++index) {
      if (sliced_pixels.at(index) != full_pixels.at(index)) {
        std::cerr << "first pixel mismatch byte=" << index
                  << " frame=" << (index / 4)
                  << " full=" << static_cast<int>(static_cast<unsigned char>(full_pixels.at(index)))
                  << " sliced=" << static_cast<int>(static_cast<unsigned char>(sliced_pixels.at(index)))
                  << '\n';
        break;
      }
    }
    return check(false, QStringLiteral("The chunk-sliced FFmpeg expression changed output pixels."));
  }
  return true;
}

bool run_805k_key_performance_probe() {
  constexpr qsizetype keyframe_count = 805000;
  constexpr double duration_seconds = 298.0;
  QVector<Keyframe> points;
  points.reserve(keyframe_count);
  for (qsizetype index = keyframe_count; index-- > 0;) {
    const auto time = static_cast<double>(index) * duration_seconds /
                      static_cast<double>(keyframe_count - 1);
    points.push_back(Keyframe{
        time,
        0.5 + 0.49 * std::sin(time * 0.17),
        Interpolation::Linear,
    });
  }
  QElapsedTimer build_timer;
  build_timer.start();
  QString error;
  auto channel = ChannelIndex::create(std::move(points), 0.0, &error);
  const auto build_ms = build_timer.elapsed();
  if (!check(channel.has_value(), QStringLiteral("The 805k-key channel should index: %1")
                                      .arg(error))) {
    return false;
  }

  QElapsedTimer windows_timer;
  windows_timer.start();
  qint64 total_window_keys = 0;
  qint64 total_expression_bytes = 0;
  qsizetype largest_window = 0;
  qsizetype largest_expression_bytes = 0;
  qint64 windows = 0;
  for (qint64 first = 0; first < kTotalFrames; first += kChunkFrames) {
    const auto end = qMin(kTotalFrames, first + kChunkFrames);
    const auto start_seconds = static_cast<double>(first) / kFps;
    const auto end_seconds = static_cast<double>(end) / kFps;
    const auto window = channel->windowForRange(start_seconds, end_seconds, &error);
    if (!window.has_value()) {
      return check(false, QStringLiteral("The 805k-key range should select: %1").arg(error));
    }
    const auto window_size = window->end - window->first;
    const auto expression = channel->ffmpegExpressionForRange(
        start_seconds, end_seconds,
        QStringLiteral("(T+%1)").arg(number(start_seconds)), &error);
    if (expression.isEmpty()) {
      return check(false, QStringLiteral("The 805k-key range expression should build: %1")
                                .arg(error));
    }
    for (auto frame = first; frame < end; ++frame) {
      const auto time = static_cast<double>(frame) / kFps;
      const auto full_value = channel->evaluate(time);
      const auto sliced_value = channel->evaluate(*window, time);
      if (!std::isfinite(full_value) || !std::isfinite(sliced_value) ||
          qAbs(full_value - sliced_value) > 1.0e-12) {
        return check(false, QStringLiteral(
                                "The 805k-key window changed sampled value at frame %1.")
                                .arg(frame));
      }
    }
    total_window_keys += window_size;
    total_expression_bytes += expression.toUtf8().size();
    largest_window = qMax(largest_window, window_size);
    largest_expression_bytes = qMax(
        largest_expression_bytes, static_cast<qsizetype>(expression.toUtf8().size()));
    ++windows;
  }
  const auto windows_ms = windows_timer.elapsed();
  const auto legacy_repeated_key_visits =
      static_cast<qint64>(channel->keyframeCount()) * windows;
  const auto reduction = static_cast<double>(legacy_repeated_key_visits) /
                         static_cast<double>(qMax<qint64>(1, total_window_keys));
  QTextStream(stdout) << "PERF_805K {\"keyframes\":" << channel->keyframeCount()
                      << ",\"windows\":" << windows
                      << ",\"indexBuildMs\":" << build_ms
                      << ",\"windowBuildMs\":" << windows_ms
                      << ",\"windowKeysProcessed\":" << total_window_keys
                      << ",\"largestWindowKeys\":" << largest_window
                      << ",\"expressionBytesTotal\":" << total_expression_bytes
                      << ",\"largestExpressionBytes\":" << largest_expression_bytes
                      << ",\"legacyWholeCurveKeyVisits\":" << legacy_repeated_key_visits
                      << ",\"keyVisitReductionEstimate\":" << reduction << "}\n";
  return check(total_window_keys <= channel->keyframeCount() + 6 * windows &&
                   largest_window < 2000 &&
                   largest_expression_bytes < 2 * 1024 * 1024,
               QStringLiteral("The 805k-key probe must keep windows bounded to nearby keyframes."));
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  if (!test_validation_and_legacy_expression_shape() ||
      !test_exact_range_evaluation() ||
      !test_ffmpeg_pixels_match_across_windows() ||
      !run_805k_key_performance_probe()) {
    return 1;
  }
  std::cout << "PASS: render-local keyframe channel index and sliced expression probe\n";
  return 0;
}
