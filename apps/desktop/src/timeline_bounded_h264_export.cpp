#include "timeline_bounded_h264_export.h"
#include "timeline_bounded_concat_clock.h"
#include "timeline_bounded_ffmpeg_memory_policy.h"
#include "timeline_shared_png_inputs.h"
#include "timeline_document_controller.h"

#include "timeline_native_generator_frame_cache.h"

#include <QDir>
#include <QDebug>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSet>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace photo_editor::desktop::timeline_motion::bounded_h264 {
namespace {

bool is_still_image_input(const QString& input_path, const QStringList& arguments,
                          qsizetype previous_input_index, qsizetype input_index) {
  if (QFileInfo(input_path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) == 0) {
    return true;
  }
  for (auto index = previous_input_index + 2; index < input_index; ++index) {
    if (arguments.at(index) == QStringLiteral("-loop")) {
      return true;
    }
    if (arguments.at(index) == QStringLiteral("-f") && index + 1 < input_index &&
        arguments.at(index + 1) == QStringLiteral("image2")) {
      return true;
    }
  }
  return false;
}

bool set_scoped_thread_option(QStringList* arguments,
                              qsizetype scope_start,
                              qsizetype scope_end,
                              int thread_count,
                              qsizetype insertion_index) {
  if (arguments == nullptr) {
    return false;
  }
  QVector<qsizetype> existing_options;
  for (auto index = scope_start; index < scope_end; ++index) {
    if (arguments->at(index) == QStringLiteral("-threads")) {
      existing_options.push_back(index);
    }
  }
  if (existing_options.isEmpty()) {
    arguments->insert(insertion_index, QStringLiteral("-threads"));
    arguments->insert(insertion_index + 1, QString::number(thread_count));
    return true;
  }
  auto effective_thread_count = thread_count;
  for (const auto index : existing_options) {
    if (index + 1 >= scope_end) {
      return false;
    }
    bool valid_count = false;
    const auto existing_count = arguments->at(index + 1).toInt(&valid_count);
    if (!valid_count) {
      return false;
    }
    if (existing_count > 0) {
      effective_thread_count = qMin(effective_thread_count, existing_count);
    }
  }
  arguments->replace(existing_options.first() + 1,
                     QString::number(effective_thread_count));
  for (qsizetype option = existing_options.size(); option-- > 1;) {
    const auto index = existing_options.at(option);
    arguments->removeAt(index + 1);
    arguments->removeAt(index);
  }
  return true;
}

bool verify_video_frame_count_and_duration(const QString& ffprobe,
                                           const QString& video_path,
                                           qint64 expected_frames,
                                           double frames_per_second,
                                           const std::function<bool()>& cancellation_requested,
                                           QString* error_message) {
  const auto fail = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
    return false;
  };
  if (ffprobe.isEmpty() || video_path.isEmpty() || expected_frames <= 0 ||
      !std::isfinite(frames_per_second) || frames_per_second <= 0.0) {
    return fail(QStringLiteral("Cannot verify the bounded H.264 output metadata."));
  }

  QProcess process;
  process.start(ffprobe, {
      QStringLiteral("-v"), QStringLiteral("error"),
      QStringLiteral("-select_streams"), QStringLiteral("v:0"),
      QStringLiteral("-threads"), QStringLiteral("1"),
      QStringLiteral("-count_frames"),
      QStringLiteral("-show_entries"), QStringLiteral("stream=nb_read_frames,duration"),
      QStringLiteral("-of"), QStringLiteral("json"), video_path,
  });
  if (!process.waitForStarted(5000)) {
    return fail(QStringLiteral("FFprobe could not inspect the assembled timeline video."));
  }
  QElapsedTimer timer;
  timer.start();
  while (process.state() != QProcess::NotRunning) {
    if (cancellation_requested && cancellation_requested()) {
      process.terminate();
      if (!process.waitForFinished(1000)) {
        process.kill();
        static_cast<void>(process.waitForFinished(1000));
      }
      return fail(QStringLiteral("Timeline render canceled."));
    }
    if (timer.elapsed() >= 120000) {
      process.kill();
      static_cast<void>(process.waitForFinished(1000));
      return fail(QStringLiteral("FFprobe timed out while verifying the assembled timeline video."));
    }
    static_cast<void>(process.waitForFinished(250));
  }
  if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
    return fail(QStringLiteral("FFprobe could not inspect the assembled timeline video."));
  }
  QJsonParseError parse_error;
  const auto document = QJsonDocument::fromJson(process.readAllStandardOutput(), &parse_error);
  const auto streams = document.object().value(QStringLiteral("streams")).toArray();
  if (parse_error.error != QJsonParseError::NoError || streams.size() != 1 ||
      !streams.first().isObject()) {
    return fail(QStringLiteral("FFprobe returned invalid video metadata for the assembled timeline."));
  }
  const auto stream = streams.first().toObject();
  bool frame_count_ok = false;
  auto frame_count = stream.value(QStringLiteral("nb_read_frames")).toString().toLongLong(
      &frame_count_ok);
  if (!frame_count_ok && stream.value(QStringLiteral("nb_read_frames")).isDouble()) {
    const auto numeric_frames = stream.value(QStringLiteral("nb_read_frames")).toDouble();
    if (std::isfinite(numeric_frames) && numeric_frames >= 0.0 &&
        numeric_frames < static_cast<double>(std::numeric_limits<qint64>::max()) &&
        std::floor(numeric_frames) == numeric_frames) {
      frame_count = static_cast<qint64>(numeric_frames);
      frame_count_ok = true;
    }
  }
  bool duration_ok = false;
  auto duration = stream.value(QStringLiteral("duration")).toString().toDouble(&duration_ok);
  if (!duration_ok && stream.value(QStringLiteral("duration")).isDouble()) {
    duration = stream.value(QStringLiteral("duration")).toDouble();
    duration_ok = std::isfinite(duration);
  }
  if (!frame_count_ok || frame_count != expected_frames) {
    return fail(QStringLiteral("The assembled timeline video contains %1 frames; expected %2.")
                    .arg(frame_count_ok ? QString::number(frame_count)
                                        : QStringLiteral("an unknown number of"))
                    .arg(expected_frames));
  }
  const auto expected_duration = static_cast<double>(expected_frames) / frames_per_second;
  const auto duration_tolerance = qMax(1.0e-5, 0.25 / frames_per_second);
  if (!duration_ok || !std::isfinite(duration) ||
      qAbs(duration - expected_duration) > duration_tolerance) {
    return fail(QStringLiteral(
        "The assembled timeline video duration does not match its expected frame clock."));
  }
  return true;
}

qint64 saturating_product(qint64 left, qint64 right) {
  if (left <= 0 || right <= 0) {
    return 0;
  }
  if (left > std::numeric_limits<qint64>::max() / right) {
    return std::numeric_limits<qint64>::max();
  }
  return left * right;
}

qint64 estimated_working_set_bytes(
    const QSize& frame_size,
    qint64 frame_count,
    qint64 source_count) {
  auto bytes = saturating_product(frame_size.width(), frame_size.height());
  bytes = saturating_product(bytes, 4);
  bytes = saturating_product(bytes, frame_count);
  return saturating_product(bytes, source_count);
}

qint64 saturating_sum(qint64 left, qint64 right) {
  if (left < 0 || right < 0 || left > std::numeric_limits<qint64>::max() - right) {
    return std::numeric_limits<qint64>::max();
  }
  return left + right;
}

}  // namespace

QStringList apply_ffmpeg_thread_limits(const QStringList& arguments,
                                       QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  auto limited = arguments;
  const auto set_error = [error_message](const QString& message) {
    if (error_message != nullptr) {
      *error_message = message;
    }
  };
  if (limited.isEmpty()) {
    set_error(QStringLiteral("The bounded FFmpeg command is empty."));
    return {};
  }

  const auto complex_graph_index = limited.indexOf(QStringLiteral("-filter_complex"));
  const auto script_graph_index = limited.indexOf(QStringLiteral("-filter_complex_script"));
  const auto graph_index = complex_graph_index < 0
                               ? script_graph_index
                               : script_graph_index < 0
                                     ? complex_graph_index
                                     : qMin(complex_graph_index, script_graph_index);
  if (graph_index < 0 || graph_index + 1 >= limited.size()) {
    set_error(QStringLiteral("The bounded FFmpeg command has no complete filter graph."));
    return {};
  }
  const auto first_input = limited.indexOf(QStringLiteral("-i"));
  if (first_input < 0) {
    set_error(QStringLiteral("The bounded FFmpeg command has no media inputs."));
    return {};
  }
  auto encoder_index = limited.indexOf(QStringLiteral("-c:v"));
  if (encoder_index < 0 || encoder_index + 1 >= limited.size() ||
      limited.at(encoder_index + 1) != QStringLiteral("libx264")) {
    set_error(QStringLiteral("The bounded FFmpeg command must use the native libx264 encoder."));
    return {};
  }

  // Filter-complex threading is a global option. Remove duplicate instances
  // and place the bounded value at the front so it cannot accidentally scope
  // after an input.
  for (auto index = limited.size(); index-- > 0;) {
    if (limited.at(index) == QStringLiteral("-filter_complex_threads")) {
      if (index + 1 >= limited.size()) {
        set_error(QStringLiteral("The filter-complex thread option is missing its value."));
        return {};
      }
      limited.removeAt(index + 1);
      limited.removeAt(index);
    }
  }
  limited.insert(0, QString::number(kBoundedFilterComplexThreads));
  limited.insert(0, QStringLiteral("-filter_complex_threads"));

  struct ImageInputScope final {
    qsizetype previous_input_index = -2;
    qsizetype input_index = -1;
  };
  QVector<ImageInputScope> image_input_scopes;
  qsizetype previous_input = -2;
  for (qsizetype index = 0; index + 1 < limited.size(); ++index) {
    if (limited.at(index) != QStringLiteral("-i")) {
      continue;
    }
    if (is_still_image_input(limited.at(index + 1), limited, previous_input, index)) {
      image_input_scopes.push_back({previous_input, index});
    }
    previous_input = index;
    ++index;
  }
  for (qsizetype index = image_input_scopes.size(); index-- > 0;) {
    const auto scope = image_input_scopes.at(index);
    if (!set_scoped_thread_option(&limited, scope.previous_input_index + 2,
                                  scope.input_index, kBoundedImageDecoderThreads,
                                  scope.input_index)) {
      set_error(QStringLiteral("A still-image input has a malformed decoder thread option."));
      return {};
    }
  }

  const auto final_input = limited.lastIndexOf(QStringLiteral("-i"));
  if (final_input < 0) {
    set_error(QStringLiteral("The bounded FFmpeg command lost its media inputs."));
    return {};
  }
  encoder_index = limited.indexOf(QStringLiteral("-c:v"));
  if (encoder_index < 0 || encoder_index + 1 >= limited.size() ||
      limited.at(encoder_index + 1) != QStringLiteral("libx264") ||
      !set_scoped_thread_option(&limited, encoder_index + 2, limited.size(),
                                kBoundedH264EncoderThreads, encoder_index + 2)) {
    set_error(QStringLiteral("The bounded FFmpeg encoder thread option is malformed."));
    return {};
  }
  return limited;
}

QStringList build_final_video_audio_mux_arguments(
    const QString& concat_path,
    const QString& audio_path,
    const QString& output_path,
    qint64 total_frames,
    int audio_sample_rate,
    const QString& audio_bitrate,
    QString* error_message) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (concat_path.trimmed().isEmpty() || audio_path.trimmed().isEmpty() ||
      output_path.trimmed().isEmpty() || total_frames <= 0 || audio_sample_rate <= 0 ||
      audio_bitrate.trimmed().isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The bounded video/audio mux settings are invalid.");
    }
    return {};
  }
  return {
      QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
      QStringLiteral("-f"), QStringLiteral("concat"), QStringLiteral("-safe"),
      QStringLiteral("1"), QStringLiteral("-i"), concat_path,
      QStringLiteral("-i"), audio_path,
      QStringLiteral("-map"), QStringLiteral("0:v:0"),
      QStringLiteral("-map"), QStringLiteral("1:a:0"),
      QStringLiteral("-c:v"), QStringLiteral("copy"),
      QStringLiteral("-c:a"), QStringLiteral("aac"),
      QStringLiteral("-ar"), QString::number(audio_sample_rate),
      QStringLiteral("-b:a"), audio_bitrate,
      QStringLiteral("-frames:v"), QString::number(total_frames),
      QStringLiteral("-color_primaries"), QStringLiteral("bt709"),
      QStringLiteral("-color_trc"), QStringLiteral("bt709"),
      QStringLiteral("-colorspace"), QStringLiteral("bt709"),
      QStringLiteral("-color_range"), QStringLiteral("tv"),
      QStringLiteral("-video_track_timescale"), QStringLiteral("240000"),
      QStringLiteral("-movflags"), QStringLiteral("+faststart"),
      QStringLiteral("-y"), output_path,
  };
}

QVector<FrameRange> plan_frame_ranges(
    qint64 total_frames,
    double frames_per_second,
    const QSize& frame_size,
    const LiveSourceCounter& live_source_counter,
    QString* error_message,
    qint64 cache_budget_bytes,
    const AdditionalSourceByteCounter& additional_source_byte_counter) {
  if (error_message != nullptr) {
    error_message->clear();
  }
  if (total_frames <= 0 || !std::isfinite(frames_per_second) ||
      frames_per_second <= 0.0 || !frame_size.isValid() ||
      frame_size.isEmpty() || !live_source_counter || cache_budget_bytes <= 0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The bounded H.264 render range settings are invalid.");
    }
    return {};
  }

  const auto effective_budget = static_cast<qint64>(
      std::floor(static_cast<double>(cache_budget_bytes) * kCacheBudgetFraction));
  const auto max_window_frames = bounded_window_frame_limit(
      frames_per_second, kMaximumRenderWindowSeconds);
  if (max_window_frames <= 0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The bounded H.264 frame window limit is invalid.");
    }
    return {};
  }
  QVector<FrameRange> ranges;
  auto start_frame = qint64{0};
  while (start_frame < total_frames) {
    auto end_frame = start_frame + qMin(max_window_frames, total_frames - start_frame);
    auto mask_cache_enabled = true;
    auto mask_fallback_reason = QString{};
    auto source_count = qint64{0};
    auto mask_source_bytes = qint64{0};
    auto estimate = qint64{0};
    const auto evaluate_range = [&](qint64 candidate_end,
                                    bool include_mask_sources,
                                    qint64* result_source_count,
                                    qint64* result_mask_bytes,
                                    qint64* result_estimate) {
      const auto count = live_source_counter(start_frame, candidate_end);
      if (count < 0) {
        return false;
      }
      const auto extra_bytes = include_mask_sources && additional_source_byte_counter
                                   ? additional_source_byte_counter(start_frame, candidate_end)
                                   : 0;
      if (extra_bytes < 0) {
        return false;
      }
      const auto base_bytes = estimated_working_set_bytes(
          frame_size, candidate_end - start_frame, count);
      if (result_source_count != nullptr) *result_source_count = count;
      if (result_mask_bytes != nullptr) *result_mask_bytes = extra_bytes;
      if (result_estimate != nullptr) *result_estimate = saturating_sum(base_bytes, extra_bytes);
      return true;
    };
    if (!evaluate_range(end_frame, true, &source_count, &mask_source_bytes, &estimate)) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral(
            "Unable to count bounded procedural and mask sources for a render window.");
      }
      return {};
    }

    if (estimate > effective_budget) {
      qint64 one_frame_source_count = 0;
      qint64 one_frame_mask_bytes = 0;
      qint64 one_frame_estimate = 0;
      if (!evaluate_range(start_frame + 1, true, &one_frame_source_count,
                          &one_frame_mask_bytes, &one_frame_estimate)) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral(
              "Unable to estimate one bounded procedural and mask frame.");
        }
        return {};
      }
      if (one_frame_estimate > effective_budget && one_frame_mask_bytes > 0) {
        // GEQ computes the same mask in the graph without retaining a second
        // decoded image sequence. Use it when even one frame of cached masks
        // would exceed the raw-source budget.
        mask_cache_enabled = false;
        mask_fallback_reason = QStringLiteral(
            "Cached mask source frames exceed the per-frame memory budget; using the native GEQ mask path.");
        if (!evaluate_range(end_frame, false, &source_count, &mask_source_bytes, &estimate) ||
            estimate > effective_budget) {
          qint64 one_frame_base_estimate = 0;
          if (!evaluate_range(start_frame + 1, false, &source_count, nullptr,
                              &one_frame_base_estimate) ||
              one_frame_base_estimate > effective_budget) {
            if (error_message != nullptr) {
              *error_message = QStringLiteral(
                  "One output frame of the live procedural sources exceeds the native render cache budget.");
            }
            return {};
          }
        }
      }
    }

    if (estimate > effective_budget) {
      qint64 low_length = 1;
      qint64 high_length = end_frame - start_frame;
      qint64 best_end = start_frame;
      while (low_length <= high_length) {
        const auto middle_length = low_length + (high_length - low_length) / 2;
        const auto candidate_end = start_frame + middle_length;
        qint64 candidate_estimate = 0;
        if (!evaluate_range(candidate_end, mask_cache_enabled, nullptr, nullptr,
                            &candidate_estimate)) {
          if (error_message != nullptr) {
            *error_message = QStringLiteral(
                "Unable to recount sources for a bounded H.264 range candidate.");
          }
          return {};
        }
        if (candidate_estimate <= effective_budget) {
          best_end = candidate_end;
          low_length = middle_length + 1;
        } else {
          high_length = middle_length - 1;
        }
      }
      if (best_end <= start_frame) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral(
              "One output frame of the live procedural sources exceeds the native render cache budget.");
        }
        return {};
      }
      end_frame = best_end;
      if (!evaluate_range(end_frame, mask_cache_enabled, &source_count,
                          &mask_source_bytes, &estimate) ||
          estimate > effective_budget) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral(
              "The bounded H.264 planner could not fit its final frame range within the cache budget.");
        }
        return {};
      }
    }
    if (end_frame <= start_frame) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("The bounded H.264 planner produced an empty frame range.");
      }
      return {};
    }
    ranges.push_back(FrameRange{
        .start_frame = start_frame,
        .end_frame = end_frame,
        .live_procedural_source_count = source_count,
        .estimated_mask_source_bytes = mask_cache_enabled ? mask_source_bytes : 0,
        .estimated_source_bytes = estimate,
        .native_mask_cache_enabled = mask_cache_enabled,
        .mask_cache_fallback_reason = mask_fallback_reason,
    });
    start_frame = end_frame;
  }
  return ranges;
}

}  // namespace photo_editor::desktop::timeline_motion::bounded_h264

namespace photo_editor::desktop {

bool TimelineDocumentController::renderTimelineVideoBoundedH264(
    const QString& output_path,
    const QString& encoder,
    const QHash<QString, QString>& video_source_overrides,
    QString* error_message,
    qint64 requested_start_frame,
    qint64 requested_end_frame_exclusive) const {
  using photo_editor::desktop::timeline_motion::NativeGeneratorFrameSequenceLease;
  using photo_editor::desktop::timeline_motion::bounded_h264::apply_ffmpeg_thread_limits;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      build_final_video_audio_mux_arguments;
  using photo_editor::desktop::timeline_motion::bounded_h264::
      kMaximumFilterGraphInputCount;
  using photo_editor::desktop::timeline_motion::bounded_h264::plan_frame_ranges;
  using photo_editor::desktop::timeline_motion::bounded_h264::share_identical_png_inputs;

  if (error_message != nullptr) {
    error_message->clear();
  }
  const auto ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
  const auto ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
  const auto output_info = QFileInfo(output_path);
  const auto output_suffix = output_info.suffix().toLower();
  if (ffmpeg.isEmpty() || ffprobe.isEmpty() || encoder != QStringLiteral("libx264") ||
      (output_suffix != QStringLiteral("mp4") &&
       output_suffix != QStringLiteral("m4v") &&
       output_suffix != QStringLiteral("mov"))) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "Bounded timeline rendering requires FFmpeg and FFprobe, the software H.264 encoder, and an MP4/MOV output path.");
    }
    return false;
  }

  const auto fps = framesPerSecond();
  const auto duration = totalDurationSeconds();
  const auto raw_total_frames = duration * fps;
  if (!std::isfinite(fps) || fps <= 0.0 || !std::isfinite(duration) ||
      duration <= 0.0 || !std::isfinite(raw_total_frames) ||
      raw_total_frames > 100000000.0) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline duration or frame rate is invalid for bounded rendering.");
    }
    return false;
  }
  const auto total_frames = static_cast<qint64>(std::ceil(raw_total_frames - 1.0e-7));
  const auto has_requested_range = requested_start_frame >= 0 ||
                                   requested_end_frame_exclusive >= 0;
  if (has_requested_range &&
      (requested_start_frame < 0 ||
       requested_end_frame_exclusive <= requested_start_frame ||
       requested_end_frame_exclusive > total_frames)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral(
          "The bounded H.264 excerpt must be a valid half-open global frame range.");
    }
    return false;
  }
  const auto range_start_frame = has_requested_range ? requested_start_frame : 0;
  const auto range_end_frame = has_requested_range ? requested_end_frame_exclusive : total_frames;
  const auto range_frame_count = range_end_frame - range_start_frame;
  const auto frame_size = nativeGeneratorOutputSize(false);
  if (total_frames <= 0 || range_frame_count <= 0 ||
      !frame_size.isValid() || frame_size.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("The timeline has no valid output frames to render.");
    }
    return false;
  }

  const auto source_count_for_range = [this, fps, duration, frame_size,
                                       range_start_frame](
                                          qint64 local_start_frame,
                                          qint64 local_end_frame) -> qint64 {
    const auto start_frame = range_start_frame + local_start_frame;
    const auto end_frame = range_start_frame + local_end_frame;
    const auto start_seconds = static_cast<double>(start_frame) / fps;
    const auto end_seconds = qMin(duration, static_cast<double>(end_frame) / fps);
    const auto requests = nativeGeneratorFrameSequenceRequestsForRange(
        start_seconds, end_seconds, frame_size, true);
    QSet<QString> unique_requests;
    for (const auto& request : requests) {
      if (request.samples.isEmpty()) {
        continue;
      }
      const auto key = photo_editor::desktop::timeline_motion::
          native_generator_frame_sequence_cache_key(request);
      if (!key.isEmpty()) {
        unique_requests.insert(key);
      }
    }
    return static_cast<qint64>(unique_requests.size());
  };
  const auto mask_source_bytes_for_range = [this, frame_size,
                                            range_start_frame](
                                              qint64 local_start_frame,
                                              qint64 local_end_frame) -> qint64 {
    return nativeMaskFrameSourcesDecodedBytesForRange(
        range_start_frame + local_start_frame,
        range_start_frame + local_end_frame, frame_size, false);
  };
  QString planner_error;
  const auto ranges = plan_frame_ranges(
      range_frame_count, fps, frame_size, source_count_for_range, &planner_error,
      photo_editor::desktop::timeline_motion::kNativeGeneratorFrameCacheBudgetBytes,
      mask_source_bytes_for_range);
  if (ranges.isEmpty()) {
    if (error_message != nullptr) {
      *error_message = planner_error.isEmpty()
                           ? QStringLiteral("The bounded timeline frame planner returned no ranges.")
                           : planner_error;
    }
    return false;
  }
  for (qsizetype range_index = 0; range_index < ranges.size(); ++range_index) {
    const auto& range = ranges.at(range_index);
    if (!range.native_mask_cache_enabled && !range.mask_cache_fallback_reason.isEmpty()) {
      qWarning().noquote() << QStringLiteral("Bounded timeline mask-cache fallback, range %1: %2")
                                  .arg(range_index)
                                  .arg(range.mask_cache_fallback_reason);
    }
  }

  QTemporaryDir temporary_directory(
      QDir::tempPath() + QStringLiteral("/looksy-bounded-h264-XXXXXX"));
  if (!temporary_directory.isValid()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to create temporary space for bounded timeline rendering.");
    }
    return false;
  }
  const auto audio_path = QDir(temporary_directory.path()).filePath(QStringLiteral("timeline-audio.wav"));
  const auto audio_range_start = has_requested_range
      ? static_cast<double>(range_start_frame) / fps
      : -1.0;
  const auto audio_range_end = has_requested_range
      ? static_cast<double>(range_end_frame) / fps
      : -1.0;
  const auto audio_arguments = timelineRenderArguments(
      audio_path, error_message, false, QStringLiteral("stem"), QString(),
      QString(), 0, audio_range_start, audio_range_end);
  if (audio_arguments.isEmpty() || !runTimelineFfmpeg(audio_arguments, error_message) ||
      !QFileInfo::exists(audio_path)) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("Unable to render the continuous timeline audio stem.");
    }
    return false;
  }

  auto pending_ranges = ranges;
  const auto report_window_timings =
      qEnvironmentVariableIntValue("PHOTO_EDITOR_TIMELINE_EXPORT_TIMINGS") == 1;
  QStringList concat_lines{QStringLiteral("ffconcat version 1.0")};
  concat_lines.reserve(ranges.size() + 1);
  const auto frame_rate_arg = QString::number(fps, 'f', 9);
  for (qsizetype range_index = 0; range_index < pending_ranges.size();) {
    if (renderCancellationRequested()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Timeline render canceled.");
      }
      return false;
    }
    const auto range = pending_ranges.at(range_index);
    const auto global_start_frame = range_start_frame + range.start_frame;
    const auto global_end_frame = range_start_frame + range.end_frame;
    const auto start_seconds = static_cast<double>(global_start_frame) / fps;
    const auto end_seconds = qMin(
        duration, static_cast<double>(global_end_frame) / fps);
    const auto frame_count = range.end_frame - range.start_frame;
    if (frame_count <= 0 || end_seconds <= start_seconds) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("The bounded timeline planner produced an invalid frame window.");
      }
      return false;
    }

    QElapsedTimer window_timer;
    window_timer.start();
    NativeGeneratorFrameSequenceLease generator_lease;
    if (!prepareNativeGeneratorFrameSequencesForRange(
            start_seconds, end_seconds, frame_size, &generator_lease, error_message)) {
      return false;
    }
    const auto generator_elapsed_ms = window_timer.elapsed();
    const auto chunk_name = QStringLiteral("video-%1.%2")
                                .arg(static_cast<qlonglong>(range_index), 6, 10, QLatin1Char('0'))
                                .arg(output_suffix);
    const auto chunk_path = QDir(temporary_directory.path()).filePath(chunk_name);
    const auto graph_path = QDir(temporary_directory.path()).filePath(
        QStringLiteral("graph-%1.ffgraph").arg(
            static_cast<qlonglong>(range_index), 6, 10, QLatin1Char('0')));
    auto chunk_arguments = timelineRenderArguments(
        chunk_path, error_message, false, QStringLiteral("bounded-h264-video"),
        QString(), encoder, 0, start_seconds, end_seconds, {}, video_source_overrides,
        graph_path, &generator_lease, range.native_mask_cache_enabled);
    if (chunk_arguments.isEmpty()) {
      QFile::remove(graph_path);
      return false;
    }
    const auto unshared_graph_input_count = static_cast<qsizetype>(std::count(
        chunk_arguments.cbegin(), chunk_arguments.cend(), QStringLiteral("-i")));
    const auto graph_option = chunk_arguments.indexOf(QStringLiteral("-filter_complex_script"));
    if (graph_option >= 0 && graph_option + 1 < chunk_arguments.size() &&
        chunk_arguments.at(graph_option + 1) == graph_path) {
      QFile graph_file(graph_path);
      if (!graph_file.open(QIODevice::ReadOnly)) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral("Unable to read the bounded timeline filter graph.");
        }
        return false;
      }
      const auto original_graph = QString::fromUtf8(graph_file.readAll());
      graph_file.close();
      const auto shared = share_identical_png_inputs(chunk_arguments, original_graph);
      if (shared.removed_inputs > 0) {
        QSaveFile rewritten_graph(graph_path);
        const auto encoded_graph = shared.graph.toUtf8();
        if (!rewritten_graph.open(QIODevice::WriteOnly) ||
            rewritten_graph.write(encoded_graph) != encoded_graph.size() ||
            !rewritten_graph.commit()) {
          if (error_message != nullptr) {
            *error_message = QStringLiteral("Unable to write the shared PNG timeline filter graph.");
          }
          return false;
        }
        chunk_arguments = shared.arguments;
        if (report_window_timings) {
          qInfo().noquote() << QStringLiteral(
              "timeline.bounded.sharedPngInputs startFrame=%1 endFrame=%2 removedInputs=%3")
                                  .arg(global_start_frame)
                                  .arg(global_end_frame)
                                  .arg(shared.removed_inputs);
        }
      }
    }
    const auto graph_elapsed_ms = window_timer.elapsed();
    const auto graph_input_count = static_cast<qsizetype>(std::count(
        chunk_arguments.cbegin(), chunk_arguments.cend(), QStringLiteral("-i")));
    // Sharing decoders does not remove every downstream per-segment buffer.
    // Preserve the existing window complexity bound until larger graphs have
    // their own measured memory qualification.
    const auto graph_complexity_count = qMax(graph_input_count, unshared_graph_input_count);
    if (graph_complexity_count > kMaximumFilterGraphInputCount) {
      QFile::remove(graph_path);
      if (frame_count <= 1) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral(
              "A single sequence frame requires %1 FFmpeg inputs, above the bounded limit of %2.")
                               .arg(graph_complexity_count)
                               .arg(kMaximumFilterGraphInputCount);
        }
        return false;
      }
      const auto midpoint_frame = range.start_frame + frame_count / 2;
      if (midpoint_frame <= range.start_frame || midpoint_frame >= range.end_frame) {
        if (error_message != nullptr) {
          *error_message = QStringLiteral(
              "The bounded renderer could not split an overfull frame window.");
        }
        return false;
      }
      auto left_range = range;
      left_range.end_frame = midpoint_frame;
      auto right_range = range;
      right_range.start_frame = midpoint_frame;
      pending_ranges.removeAt(range_index);
      pending_ranges.insert(range_index, right_range);
      pending_ranges.insert(range_index, left_range);
      qWarning().noquote()
          << QStringLiteral(
                 "Splitting bounded H.264 window %1 (%2 inputs) at global frame %3 to respect the %4-input graph cap.")
                 .arg(range_index)
                 .arg(graph_complexity_count)
                 .arg(range_start_frame + midpoint_frame)
                 .arg(kMaximumFilterGraphInputCount);
      continue;
    }
    if (!chunk_arguments.contains(QStringLiteral("-an")) ||
        !chunk_arguments.contains(QStringLiteral("[outv]")) ||
        chunk_arguments.contains(QStringLiteral("[outa]"))) {
      QFile::remove(graph_path);
      if (error_message != nullptr) {
        *error_message = QStringLiteral(
            "The bounded timeline video graph did not provide a video-only output contract.");
      }
      return false;
    }
    auto encoded_chunk_arguments = chunk_arguments;
    encoded_chunk_arguments = apply_ffmpeg_thread_limits(
        encoded_chunk_arguments, error_message);
    if (encoded_chunk_arguments.isEmpty()) {
      QFile::remove(graph_path);
      return false;
    }
    const auto overwrite_index = encoded_chunk_arguments.indexOf(QStringLiteral("-y"));
    if (overwrite_index < 0) {
      QFile::remove(graph_path);
      if (error_message != nullptr) {
        *error_message = QStringLiteral("The bounded timeline video graph omitted its output target.");
      }
      return false;
    }
    QStringList frame_guard;
    if (!encoded_chunk_arguments.contains(QStringLiteral("-r"))) {
      frame_guard << QStringLiteral("-r") << frame_rate_arg;
    }
    if (!encoded_chunk_arguments.contains(QStringLiteral("-fps_mode"))) {
      frame_guard << QStringLiteral("-fps_mode") << QStringLiteral("cfr");
    }
    frame_guard << QStringLiteral("-frames:v") << QString::number(frame_count);
    if (!encoded_chunk_arguments.contains(QStringLiteral("-video_track_timescale"))) {
      frame_guard << QStringLiteral("-video_track_timescale") << QStringLiteral("240000");
    }
    for (qsizetype option_index = 0; option_index < frame_guard.size(); ++option_index) {
      encoded_chunk_arguments.insert(overwrite_index + option_index, frame_guard.at(option_index));
    }
    if (!runTimelineFfmpeg(encoded_chunk_arguments, error_message) ||
        !QFileInfo::exists(chunk_path) || QFileInfo(chunk_path).size() <= 0) {
      QFile::remove(graph_path);
      if (error_message != nullptr && error_message->isEmpty()) {
        *error_message = QStringLiteral("Unable to render a bounded timeline video window.");
      }
      return false;
    }
    const auto encode_elapsed_ms = window_timer.elapsed();
    QFile::remove(graph_path);
    concat_lines.push_back(QStringLiteral("file '%1'").arg(chunk_name));
    const auto duration_directive =
        timeline_motion::bounded_h264::concat_frame_duration_directive(
            range.start_frame, range.end_frame, fps);
    if (duration_directive.isEmpty()) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("The bounded timeline window has no valid frame-clock duration.");
      }
      return false;
    }
    concat_lines.push_back(duration_directive);
    if (report_window_timings) {
      qInfo().noquote()
          << QStringLiteral(
                 "timeline.bounded.window startFrame=%1 endFrame=%2 generatorMs=%3 graphMs=%4 encodeMs=%5 totalMs=%6 inputs=%7")
                 .arg(global_start_frame)
                 .arg(global_end_frame)
                 .arg(generator_elapsed_ms)
                 .arg(graph_elapsed_ms - generator_elapsed_ms)
                 .arg(encode_elapsed_ms - graph_elapsed_ms)
                 .arg(window_timer.elapsed())
                 .arg(graph_input_count);
    }
    ++range_index;
  }

  const auto concat_path = QDir(temporary_directory.path()).filePath(QStringLiteral("video.ffconcat"));
  QSaveFile concat_file(concat_path);
  const auto concat_contents = (concat_lines.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
  if (!concat_file.open(QIODevice::WriteOnly) ||
      concat_file.write(concat_contents) != concat_contents.size() ||
      !concat_file.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to write the bounded timeline video playlist.");
    }
    return false;
  }

  const auto staged_output_path = QDir(temporary_directory.path()).filePath(
      QStringLiteral("assembled.%1").arg(output_suffix));
  const auto audio_bitrate = export_preset_ == QStringLiteral("youtube")
                                 ? QStringLiteral("384k")
                                 : QStringLiteral("192k");
  const auto final_arguments = build_final_video_audio_mux_arguments(
      concat_path, audio_path, staged_output_path, range_frame_count,
      audio_sample_rate_, audio_bitrate, error_message);
  if (final_arguments.isEmpty()) {
    return false;
  }
  if (!runTimelineFfmpeg(final_arguments, error_message) ||
      !QFileInfo::exists(staged_output_path) || QFileInfo(staged_output_path).size() <= 0) {
    if (error_message != nullptr && error_message->isEmpty()) {
      *error_message = QStringLiteral("Unable to assemble the bounded timeline video and audio.");
    }
    return false;
  }
  if (!timeline_motion::bounded_h264::verify_video_frame_count_and_duration(
          ffprobe, staged_output_path, range_frame_count, fps,
          [this]() { return renderCancellationRequested(); }, error_message)) {
    QFile::remove(staged_output_path);
    return false;
  }

  QFile staged_output(staged_output_path);
  if (!staged_output.open(QIODevice::ReadOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to read the completed bounded timeline render.");
    }
    return false;
  }
  QSaveFile committed_output(output_info.absoluteFilePath());
  if (!committed_output.open(QIODevice::WriteOnly)) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to stage the completed timeline render at its destination.");
    }
    return false;
  }
  while (!staged_output.atEnd()) {
    if (renderCancellationRequested()) {
      committed_output.cancelWriting();
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Timeline render canceled.");
      }
      return false;
    }
    const auto bytes = staged_output.read(1024 * 1024);
    if (bytes.isEmpty() && staged_output.error() != QFileDevice::NoError) {
      committed_output.cancelWriting();
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to copy the completed timeline render.");
      }
      return false;
    }
    if (!bytes.isEmpty() && committed_output.write(bytes) != bytes.size()) {
      committed_output.cancelWriting();
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to write the completed timeline render.");
      }
      return false;
    }
  }
  if (!committed_output.commit()) {
    if (error_message != nullptr) {
      *error_message = QStringLiteral("Unable to commit the completed timeline render.");
    }
    return false;
  }
  return true;
}

}  // namespace photo_editor::desktop
