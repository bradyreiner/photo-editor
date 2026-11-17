#include "editor_preview_controller.h"
#include "source_image_decode.h"
#include "raw_image_decoder.h"
#include "raw_decode_policy.h"
#include "edit_recipe_v13_persistence.h"
#include "fixtures/synthetic_dng.h"

#include <QDateTime>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

namespace {
using namespace photo_editor::desktop;

constexpr int kWidth = 1600;
constexpr int kHeight = 1047;
constexpr int kPreviewEdge = 1600;
constexpr int kBenchmarkTrials = 3;

bool require(bool condition, const char* message) {
  if (!condition) std::cerr << message << '\n';
  return condition;
}

bool write_source_image(const QString& path, bool alternate_pixels = false) {
  QImage image(kWidth, kHeight, QImage::Format_RGB32);
  if (image.isNull()) return false;
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      const auto red = alternate_pixels ? (x * 7 + y * 13 + 91) % 256
                                        : (x * 13 + y * 3 + 17) % 256;
      const auto green = alternate_pixels ? (x * 3 + y * 11 + 37) % 256
                                          : (x * 5 + y * 17 + 73) % 256;
      const auto blue = alternate_pixels ? (x * 19 + y * 2 + 151) % 256
                                         : (x * 2 + y * 7 + 29) % 256;
      image.setPixel(x, y, qRgb(red, green, blue));
    }
  }
  return image.save(path, "PNG");
}

EditRecipeState state_for_exposure(double exposure) {
  EditRecipeState state;
  state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  state.input_color_profile = QStringLiteral("Embedded");
  state.working_color_profile = QStringLiteral("Linear ProPhoto RGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.exposure = exposure;
  return state;
}

RenderRequest make_request(
    const QString& path,
    const EditRecipeState& state,
    int generation,
    bool draft_interactive = true,
    bool full_resolution = false,
    int max_edge = kPreviewEdge) {
  auto latest_generation = std::make_shared<std::atomic<int>>(generation);
  return RenderRequest{
      .source_asset_id = QStringLiteral("preview-linear-cache-fixture"),
      .recipe_cache_identity = QStringLiteral("preview-linear-cache-fixture-%1").arg(generation),
      .adjusted_recipe_cache_identity =
          QStringLiteral("preview-linear-cache-adjusted-fixture-%1").arg(generation),
      .file_path = path,
      .render_reason = QStringLiteral("edit_change"),
      .interaction_mode = draft_interactive ? QStringLiteral("draft_interactive")
                                            : QStringLiteral("interactive"),
      .render_mode = draft_interactive ? QStringLiteral("live_preview")
                                       : full_resolution ? QStringLiteral("max_preview")
                                                         : QStringLiteral("settled_preview"),
      .tool_id = QStringLiteral("global_sliders"),
      .render_priority = 1,
      .dirty_domain = QStringLiteral("global_tone_color"),
      .state = state,
      .full_resolution = full_resolution,
      .max_edge = max_edge,
      .generation = generation,
      .render_latency_budget_ms = 1000,
      .use_in_memory_preview = true,
      .draft_interactive = draft_interactive,
      .require_cpu_preview_image = true,
      .require_cpu_display_image = true,
      .require_canonical_render_plan = true,
      .scope_domain = QStringLiteral("final-display"),
      .latest_generation = std::move(latest_generation),
  };
}

bool warm_decoded_source(const QString& path, const EditRecipeState& state) {
  QImage decoded;
  SourceDecodeTelemetry telemetry;
  return decode_source_image(
             path, &decoded, kPreviewEdge, state, nullptr, {}, &telemetry) &&
         !decoded.isNull();
}

RenderResult render(
    const QString& path,
    const EditRecipeState& state,
    int generation,
    bool draft_interactive = true,
    bool full_resolution = false,
    int max_edge = kPreviewEdge) {
  return render_preview_request_for_test(
      make_request(path, state, generation, draft_interactive, full_resolution, max_edge));
}

bool pixels_equal(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto lhs = left.convertToFormat(QImage::Format_RGBA8888);
  const auto rhs = right.convertToFormat(QImage::Format_RGBA8888);
  if (lhs.isNull() || rhs.isNull() || lhs.sizeInBytes() != rhs.sizeInBytes()) return false;
  const auto byte_count = static_cast<std::size_t>(lhs.sizeInBytes());
  return std::equal(lhs.constBits(), lhs.constBits() + byte_count, rhs.constBits());
}

qint64 median(std::vector<qint64> values) {
  std::sort(values.begin(), values.end());
  return values.at(values.size() / 2);
}

qint64 timed_render_ns(
    const QString& path,
    const EditRecipeState& state,
    int generation,
    RenderResult* result) {
  QElapsedTimer timer;
  timer.start();
  *result = render(path, state, generation);
  return timer.nsecsElapsed();
}

}  // namespace

#include "raw_highlight_preview_proof.inc"

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 2;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_BACKUP_DIR", runtime.filePath(QStringLiteral("backups")).toUtf8());
  qputenv("PHOTO_EDITOR_MANAGED_MEDIA_DIR", runtime.filePath(QStringLiteral("managed")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", QByteArrayLiteral("1"));
  qputenv("PHOTO_EDITOR_DATABASE_URL", QByteArrayLiteral("postgresql://127.0.0.1:1/linear-cache-test"));
  QGuiApplication app(argc, argv);
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                     runtime.filePath(QStringLiteral("preferences")));

  if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--raw-highlight")) {
    return run_raw_highlight_preview_proof(argc >= 3 ? QString::fromLocal8Bit(argv[2]) : QString{});
  }

  if (argc >= 2 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--raw-exposure")) {
    if (!raw_camera_linear_decoder_capability().available) return 77;
    auto source = runtime.filePath(QStringLiteral("exposure.dng"));
    if (argc >= 3) {
      source = QString::fromLocal8Bit(argv[2]);
    } else {
      const auto payload = photo_editor::test_fixtures::synthetic_dng();
      QFile fixture(source);
      if (!fixture.open(QIODevice::WriteOnly) ||
          fixture.write(reinterpret_cast<const char*>(payload.data()),
                        static_cast<qint64>(payload.size())) !=
              static_cast<qint64>(payload.size())) return 1;
    }
    QFile exposure_fixture(source);
    QCryptographicHash exposure_fixture_hash(QCryptographicHash::Sha256);
    if (!exposure_fixture.open(QIODevice::ReadOnly) || !exposure_fixture_hash.addData(&exposure_fixture)) return 1;
    std::cout << "raw_exposure_fixture sha256="
              << exposure_fixture_hash.result().toHex().constData() << std::endl;
    auto generation = 1;
    {
      // Disable reconstruction for this source-cache probe: reconstruction
      // deliberately retains full-source analysis outside the draft cache.
      auto neutral = state_for_exposure(0.0);
      neutral.raw_highlight_recovery = false;
      const auto warm = render(source, neutral, generation++, true, false, 512);
      auto raw_state = neutral;
      raw_state.raw_exposure = 0.5;
      const auto raw = render(source, raw_state, generation++, true, false, 512);
      auto reference_state = neutral;
      reference_state.exposure = 0.5;
      const auto reference = render(source, reference_state, generation++, true, false, 512);
      raw_state.raw_exposure = 1.5;
      const auto changed = render(source, raw_state, generation++, true, false, 512);
      if (!require(warm.success && raw.success && reference.success && changed.success &&
                       raw.linear_cache_lookup && raw.linear_cache_hit &&
                       reference.linear_cache_lookup && reference.linear_cache_hit &&
                       changed.linear_cache_lookup && changed.linear_cache_hit &&
                       pixels_equal(raw.preview_image, reference.preview_image) &&
                       !pixels_equal(raw.preview_image, changed.preview_image),
                   "actual RAW EV edits must reuse decoded linear data and invalidate adjusted output")) return 1;
      std::cout << "raw_exposure_source_cache_reused source_is_raw=1 raw_ev=0.5 next_raw_ev=1.5"
                   " source_hit=1 reference_hit=1 output_changed=1" << std::endl;
    }
    for (const auto draft : {true, false}) {
      for (const auto ev : {1.9, -2.0, 4.0}) {
        auto raw_state = state_for_exposure(0.0);
        raw_state.raw_exposure = ev;
        const auto raw = render(source, raw_state, generation++, draft, false, 512);
        const auto reference = render(source, state_for_exposure(ev), generation++, draft, false, 512);
        if (!require(raw.success && reference.success &&
                         !raw.preview_image.isNull() &&
                         pixels_equal(raw.preview_image, reference.preview_image),
                     "canonical RAW exposure preview must match float Light exposure")) {
          std::cerr << "draft=" << draft << " ev=" << ev << " status="
                    << raw.status.toStdString() << " failure="
                    << raw.failure_detail.toStdString() << '\n';
          return 1;
        }
        std::cout << "raw_exposure_preview_parity draft=" << draft << " ev=" << ev << std::endl;
      }
      for (const auto input_transform : {true, false}) {
        for (const auto& exposure : {std::array{1.9, 0.5}, std::array{1.9, -1.9}}) {
          auto combined_state = state_for_exposure(exposure[1]);
          combined_state.raw_exposure = exposure[0];
          combined_state.raw_input_transform_enabled = input_transform;
          auto reference_state = state_for_exposure(exposure[0] + exposure[1]);
          reference_state.raw_input_transform_enabled = input_transform;
          const auto combined = render(source, combined_state, generation++, draft, false, 512);
          const auto reference = render(source, reference_state, generation++, draft, false, 512);
          photo_editor::pipeline::DecodedImageMetadata metadata;
          if (!read_raw_decoded_metadata(source, &metadata, raw_decode_settings_for_state(combined_state))) {
            return require(false, "combined RAW exposure must resolve real decoder provenance") ? 0 : 1;
          }
          DesktopRecipeV13Context context;
          context.recipe_id = QStringLiteral("combined-exposure-reopen");
          context.source_id = QStringLiteral("combined-exposure-source");
          context.decoder_provenance = QString::fromStdString(metadata.decoder_backend);
          context.raw_source = true;
          const auto saved = serialize_desktop_recipe_as_canonical_v13(combined_state, context);
          const auto parsed = photo_editor::pipeline::deserialize_edit_recipe_v13(
              saved.document.recipe_payload.toStdString());
          if (!saved.ok() || !parsed) {
            return require(false, "combined RAW and Light exposure must serialize canonically") ? 0 : 1;
          }
          const auto restored = project_canonical_v13_to_desktop_state(*parsed);
          if (!restored.ok()) {
            return require(false, "combined exposure canonical recipe must reopen") ? 0 : 1;
          }
          const auto resaved = serialize_desktop_recipe_as_canonical_v13(*restored.state, context);
          const auto reopened = render(source, *restored.state, generation++, draft, false, 512);
          auto reset_state = *restored.state;
          reset_state.exposure = 0.0;
          reset_state.raw_exposure = 0.0;
          auto neutral_state = state_for_exposure(0.0);
          neutral_state.raw_input_transform_enabled = input_transform;
          const auto reset = render(source, reset_state, generation++, draft, false, 512);
          const auto neutral = render(source, neutral_state, generation++, draft, false, 512);
          auto cancelled_request = make_request(source, combined_state, generation++, draft, false, 512);
          cancelled_request.latest_generation->store(cancelled_request.generation + 1);
          const auto cancelled = render_preview_request_for_test(cancelled_request);
          if (!require(combined.success && reference.success && reopened.success && reset.success &&
                           neutral.success && !combined.preview_image.isNull() &&
                           pixels_equal(combined.preview_image, reference.preview_image) &&
                           pixels_equal(reopened.preview_image, reference.preview_image) &&
                           pixels_equal(reset.preview_image, neutral.preview_image) &&
                           resaved.ok() && resaved.document.recipe_payload == saved.document.recipe_payload &&
                           resaved.document.cache_key == saved.document.cache_key &&
                           !cancelled.success && cancelled.preview_image.isNull(),
                       "combined RAW exposure must preserve canonical preview, reopen, reset and cancellation")) {
            std::cerr << "draft=" << draft << " input_transform=" << input_transform
                      << " raw_ev=" << exposure[0] << " light_ev=" << exposure[1]
                      << " status=" << combined.status.toStdString()
                      << " failure=" << combined.failure_detail.toStdString()
                      << " reopened_status=" << reopened.status.toStdString()
                      << " reopened_failure=" << reopened.failure_detail.toStdString()
                      << " reset_status=" << reset.status.toStdString() << '\n';
            return 1;
          }
          std::cout << "raw_exposure_combined_preview_parity draft=" << draft
                    << " input_transform=" << input_transform << " raw_ev=" << exposure[0]
                    << " light_ev=" << exposure[1] << " total_ev=" << exposure[0] + exposure[1]
                    << " reopened=1 reset=1 cancelled=1" << std::endl;
        }
      }
    }
    // Canonical storage has one EV, but the restored UI must still fit its
    // existing RAW +/-4 and Light +/-5 controls at the combined +/-9 limits.
    for (const auto input_transform : {true, false}) {
      for (const auto sign : {1.0, -1.0}) {
        EditorPreviewController restored_controller;
        auto limits_state = state_for_exposure(sign * 5.0);
        limits_state.raw_exposure = sign * 4.0;
        limits_state.raw_input_transform_enabled = input_transform;
        DesktopRecipeV13Context context;
        context.recipe_id = QStringLiteral("raw-exposure-control-limits");
        context.source_id = QStringLiteral("raw-exposure-control-limits-source-%1-%2")
                                .arg(input_transform).arg(sign > 0.0 ? "positive" : "negative");
        context.decoder_provenance = QStringLiteral("LibRaw:unexposed-float-boundary");
        context.raw_source = true;
        const auto saved = serialize_desktop_recipe_as_canonical_v13(limits_state, context);
        const auto parsed = photo_editor::pipeline::deserialize_edit_recipe_v13(
            saved.document.recipe_payload.toStdString());
        if (!saved.ok() || !parsed) return 1;
        const auto restored = project_canonical_v13_to_desktop_state(*parsed);
        if (!restored.ok()) return 1;
        restored_controller.loadAsset(context.source_id, source, {}, {}, {}, {},
                                      serialize_edit_recipe_json(*restored.state));
        if (!require(restored_controller.rawExposure() == sign * 4.0 &&
                         restored_controller.exposure() == sign * 5.0,
                     "canonical extreme EV must reopen into bounded visible controls")) return 1;
        restored_controller.setExposure(sign * 4.75);
        auto edited = *restored.state;
        edited.raw_exposure = restored_controller.rawExposure();
        edited.exposure = restored_controller.exposure();
        const auto adjusted = render(source, edited, generation++, false, false, 512);
        auto reference_state = state_for_exposure(sign * 8.75);
        reference_state.raw_input_transform_enabled = input_transform;
        const auto reference = render(source, reference_state, generation++, false, false, 512);
        if (!require(edited.raw_exposure == sign * 4.0 && edited.exposure == sign * 4.75 &&
                         adjusted.success && reference.success &&
                         pixels_equal(adjusted.preview_image, reference.preview_image),
                     "editing restored Light EV must retain the RAW contribution and exact total gain")) return 1;
        std::cout << "raw_exposure_restored_controls_editable input_transform=" << input_transform
                  << " total_ev=" << sign * 9.0 << " edited_total_ev=" << sign * 8.75
                  << " raw_ev=" << edited.raw_exposure << " light_ev=" << edited.exposure << std::endl;
      }
    }
    return 0;
  }

  const auto source_a = runtime.filePath(QStringLiteral("source-a.png"));
  const auto source_b = runtime.filePath(QStringLiteral("source-b.png"));
  if (!write_source_image(source_a) || !write_source_image(source_b)) {
    return require(false, "linear-cache fixtures must be written") ? 0 : 1;
  }

  const auto warm_state = state_for_exposure(0.0);
  if (!warm_decoded_source(source_a, warm_state)) {
    return require(false, "the candidate source must have a warm decoded-QImage entry") ? 0 : 1;
  }
  auto generation = 1;
  const auto warm_result = render(source_a, warm_state, generation++);
  if (!require(warm_result.success && !warm_result.preview_image.isNull(),
               "the first canonical render must produce a CPU preview") ||
      !require(warm_result.draft_interactive && !warm_result.full_resolution,
               "the cache-populating fixture must be an interactive draft") ||
      !require(warm_result.linear_cache_lookup && !warm_result.linear_cache_hit,
               "the first canonical draft must populate a missing linear-source cache entry") ||
      !require(warm_result.canonical_draft_linear_source_cache_bytes > 0 &&
                   warm_result.canonical_draft_linear_source_cache_bytes <= 32 * 1024 * 1024,
               "the retained canonical draft source must stay within the 32 MiB cap")) {
    return 1;
  }
  const auto warm_draft_cache_bytes = warm_result.canonical_draft_linear_source_cache_bytes;

  std::vector<qint64> reference_ns;
  std::vector<qint64> candidate_ns;
  std::vector<qint64> reference_linear_ms;
  std::vector<qint64> candidate_linear_ms;
  reference_ns.reserve(kBenchmarkTrials);
  candidate_ns.reserve(kBenchmarkTrials);
  reference_linear_ms.reserve(kBenchmarkTrials);
  candidate_linear_ms.reserve(kBenchmarkTrials);
  const std::vector<double> exposures{0.0, 0.36, 0.76};
  for (int trial = 0; trial < kBenchmarkTrials; ++trial) {
    const auto state = state_for_exposure(exposures.at(static_cast<std::size_t>(trial)));
    const auto reference_path = runtime.filePath(
        QStringLiteral("decoded-warm-linear-miss-%1.png").arg(trial));
    if (!write_source_image(reference_path) || !warm_decoded_source(reference_path, state)) {
      return require(false, "reference image decode must be warm before linear-miss timing") ? 0 : 1;
    }

    auto candidate_warm_result = RenderResult{};
    auto candidate_result = RenderResult{};
    auto reference_result = RenderResult{};
    qint64 candidate_elapsed = 0;
    qint64 reference_elapsed = 0;
    if (trial % 2 == 0) {
      reference_elapsed = timed_render_ns(reference_path, state, generation++, &reference_result);
      candidate_warm_result = render(source_a, state, generation++);
      if (!require(candidate_warm_result.success,
                   "the candidate source must be installed after the reference cache miss")) {
        return 1;
      }
      candidate_elapsed = timed_render_ns(source_a, state, generation++, &candidate_result);
    } else {
      candidate_warm_result = render(source_a, state, generation++);
      if (!require(candidate_warm_result.success,
                   "the candidate source must be installed before its timed hit")) {
        return 1;
      }
      candidate_elapsed = timed_render_ns(source_a, state, generation++, &candidate_result);
      reference_elapsed = timed_render_ns(reference_path, state, generation++, &reference_result);
    }
    if (!require(candidate_result.success && reference_result.success,
                 "matched candidate and warm-decoded reference renders must succeed") ||
        !require(candidate_result.linear_cache_lookup && candidate_result.linear_cache_hit,
                 "global Exposure changes must reuse the exact cached linear input") ||
        !require(!candidate_result.adjusted_cache_hit && !reference_result.adjusted_cache_hit,
                 "adjusted working-image cache hits must not short-circuit the measured Decode stage") ||
        !require(candidate_result.completed_render_stages.contains(QStringLiteral("decode")) &&
                     reference_result.completed_render_stages.contains(QStringLiteral("decode")),
                 "both timed frames must execute the canonical Decode stage") ||
        !require(reference_result.linear_cache_lookup && !reference_result.linear_cache_hit,
                 "the matched draft reference must miss only the one-entry linear-source cache") ||
        !require(reference_result.decoded_cache_hit,
                 "the linear-miss reference must use its warm decoded-QImage entry") ||
        !require(candidate_result.decoded_cache_hit,
                 "the draft candidate must use its warm decoded-QImage entry") ||
        !require(!candidate_result.rendered_cache_hit && !reference_result.rendered_cache_hit,
                 "timed candidate and reference renders must not reuse rendered images") ||
        !require(candidate_result.canonical_draft_linear_source_cache_bytes ==
                     warm_draft_cache_bytes &&
                     reference_result.canonical_draft_linear_source_cache_bytes ==
                         warm_draft_cache_bytes,
                 "the candidate draft cache entry must stay within its retained 32 MiB size across Exposure edits") ||
        !require(pixels_equal(candidate_result.preview_image, reference_result.preview_image),
                 "warm linear-cache output must be byte-identical to warm-decode linearization")) {
      std::cerr << "trial=" << trial
                << " candidate_linear_ms=" << candidate_result.adjustment_linear_stage_elapsed_ms
                << " reference_linear_ms=" << reference_result.adjustment_linear_stage_elapsed_ms
                << " candidate_backend=" << candidate_result.backend.toStdString()
                << " reference_backend=" << reference_result.backend.toStdString()
                << " candidate_size=" << candidate_result.preview_image.width() << 'x'
                << candidate_result.preview_image.height()
                << " reference_size=" << reference_result.preview_image.width() << 'x'
                << reference_result.preview_image.height() << '\n';
      return 1;
    }
    candidate_ns.push_back(candidate_elapsed);
    reference_ns.push_back(reference_elapsed);
    candidate_linear_ms.push_back(candidate_result.adjustment_linear_stage_elapsed_ms);
    reference_linear_ms.push_back(reference_result.adjustment_linear_stage_elapsed_ms);
  }

  const auto settled_result = render(
      source_a, state_for_exposure(0.36), generation++, false, false, kPreviewEdge);
  const auto full_result = render(
      source_a, state_for_exposure(0.36), generation++, false, true, 0);
  if (!require(settled_result.success && !settled_result.linear_cache_lookup &&
                   settled_result.canonical_draft_linear_source_cache_bytes == warm_draft_cache_bytes,
               "settled previews must not look up, replace, or grow the draft source cache") ||
      !require(full_result.success && full_result.full_resolution && !full_result.draft_interactive,
               "a full-resolution canonical frame must complete through the uncached path") ||
      !require(full_result.canonical_draft_linear_source_cache_bytes == warm_draft_cache_bytes,
               "full-resolution frames must not replace or grow the scoped draft source cache")) {
    return 1;
  }

  const auto changed_source = runtime.filePath(QStringLiteral("changed-source.png"));
  if (!write_source_image(changed_source, true) || !QFile::remove(source_a) ||
      !QFile::copy(changed_source, source_a)) {
    return require(false, "changed source pixels must be installed at the cached source path") ? 0 : 1;
  }
  QFile changed_source_file(source_a);
  static_cast<void>(changed_source_file.setFileTime(
      QDateTime::currentDateTimeUtc().addSecs(3), QFileDevice::FileModificationTime));
  const auto changed_source_result = render(source_a, state_for_exposure(0.36), generation++);
  if (!require(changed_source_result.success && changed_source_result.linear_cache_lookup &&
                   !changed_source_result.linear_cache_hit &&
                   changed_source_result.canonical_draft_linear_source_cache_bytes > 0 &&
                   changed_source_result.canonical_draft_linear_source_cache_bytes <= 32 * 1024 * 1024,
               "source fingerprint changes must invalidate cached linear pixels")) {
    return 1;
  }

  const auto raw_changed_result = render(
      source_a,
      [&] {
        auto state = state_for_exposure(0.0);
        state.raw_exposure = 0.35;
        return state;
      }(),
      generation++);
  if (!require(raw_changed_result.success && raw_changed_result.linear_cache_lookup &&
                   raw_changed_result.linear_cache_hit &&
                   !pixels_equal(raw_changed_result.preview_image, changed_source_result.preview_image) &&
                   raw_changed_result.canonical_draft_linear_source_cache_bytes > 0 &&
                   raw_changed_result.canonical_draft_linear_source_cache_bytes <= 32 * 1024 * 1024,
               "deferred RAW EV must reuse linear source pixels rather than invalidate decoded input")) {
    std::cerr << "raw_guard success=" << raw_changed_result.success
              << " lookup=" << raw_changed_result.linear_cache_lookup
              << " hit=" << raw_changed_result.linear_cache_hit
              << " bytes=" << raw_changed_result.canonical_draft_linear_source_cache_bytes
              << " backend=" << raw_changed_result.backend.toStdString()
              << " status=" << raw_changed_result.status.toStdString()
              << " failure=" << raw_changed_result.failure_stage.toStdString() << '/'
              << raw_changed_result.failure_detail.toStdString() << '\n';
    return 1;
  }

  // Tone edits replace adjusted output while reusing the same linear source.
  const auto crop_base = render(source_a, state_for_exposure(0.36), generation++);
  if (!require(crop_base.success && crop_base.linear_cache_lookup && crop_base.linear_cache_hit,
               "restoring prior exposure must reuse the scoped linear source entry")) return 1;
  auto cropped_state = state_for_exposure(0.36);
  cropped_state.crop_rect_x = 0.08;
  cropped_state.crop_rect_y = 0.1;
  cropped_state.crop_rect_width = 0.78;
  cropped_state.crop_rect_height = 0.72;
  const auto crop_result = render(source_a, cropped_state, generation++);
  if (!require(crop_result.success && crop_result.linear_cache_lookup &&
                   !crop_result.linear_cache_hit &&
                   crop_result.canonical_draft_linear_source_cache_bytes > 0 &&
                   crop_result.canonical_draft_linear_source_cache_bytes <= 32 * 1024 * 1024,
               "crop changes must remain part of draft linear-cache identity")) {
    return 1;
  }

  const auto ns_to_ms = [](qint64 value) {
    return static_cast<double>(value) / 1'000'000.0;
  };
  const auto reference_median_ns = median(reference_ns);
  const auto candidate_median_ns = median(candidate_ns);
  const auto improvement = reference_median_ns > 0
      ? 100.0 * static_cast<double>(reference_median_ns - candidate_median_ns) /
            static_cast<double>(reference_median_ns)
      : 0.0;
  std::cout << "preview_linear_cache_benchmark size=1600x1047 trials=" << kBenchmarkTrials
            << " reference_median_ms=" << ns_to_ms(reference_median_ns)
            << " candidate_median_ms=" << ns_to_ms(candidate_median_ns)
            << " improvement_percent=" << improvement
            << " reference_linear_stage_median_ms=" << median(reference_linear_ms)
            << " candidate_linear_stage_median_ms=" << median(candidate_linear_ms)
            << " reference_linear_cache=miss decoded_cache=hit candidate_linear_cache=hit"
            << '\n';
  return 0;
}
