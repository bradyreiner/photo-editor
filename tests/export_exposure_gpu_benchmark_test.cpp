#include "edit_recipe_state.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_render_utils.h"
#include "render_plan_store.h"
#include "working_image.h"

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_pipeline.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPoint>
#include <QRect>
#include <QTemporaryDir>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <tuple>
#include <vector>

#include <sys/resource.h>

namespace {

using photo_editor::desktop::EditRecipeState;
using photo_editor::desktop::RenderBackendPreference;
using photo_editor::desktop::RenderBackendTelemetry;
using photo_editor::desktop::RenderOutputPolicy;
using photo_editor::pipeline::ColorPixel;

constexpr int kDefaultWidth = 2048;
constexpr int kDefaultHeight = 1536;
constexpr int kMeasuredIterations = 3;
constexpr int kCanonicalMeasuredIterations = 7;
constexpr float kFloatParityTolerance = 2.0e-6F;

struct TimedImage final {
  QImage image;
  RenderBackendTelemetry telemetry;
  double elapsed_ms = 0.0;
};

QStringList arguments_for(const QCoreApplication& app) {
  return app.arguments();
}

bool parse_positive_int(
    const QStringList& arguments,
    const QString& option,
    int* value) {
  const auto index = arguments.indexOf(option);
  if (index < 0) {
    return true;
  }
  if (value == nullptr || index + 1 >= arguments.size()) {
    return false;
  }
  bool ok = false;
  const auto parsed = arguments[index + 1].toInt(&ok);
  if (!ok || parsed <= 0) {
    return false;
  }
  *value = parsed;
  return true;
}

QString report_path_for(const QStringList& arguments) {
  const auto index = arguments.indexOf(QStringLiteral("--report"));
  return index >= 0 && index + 1 < arguments.size()
      ? arguments[index + 1]
      : QString{};
}

std::uint64_t peak_rss_bytes() {
  auto usage = rusage{};
  if (getrusage(RUSAGE_SELF, &usage) != 0 || usage.ru_maxrss < 0) {
    return 0;
  }
#if defined(__APPLE__)
  return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
  return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024U;
#endif
}

QImage deterministic_sdr_source(int width, int height) {
  auto image = QImage(width, height, QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (auto y = 0; y < height; ++y) {
    auto* row = image.scanLine(y);
    for (auto x = 0; x < width; ++x) {
      const auto offset = x * 4;
      row[offset] = static_cast<uchar>(16 + ((x * 37 + y * 19) % 224));
      row[offset + 1] = static_cast<uchar>(12 + ((x * 13 + y * 53) % 232));
      row[offset + 2] = static_cast<uchar>(8 + ((x * 71 + y * 29) % 240));
      const auto alpha_bucket = (x + 3 * y) % 7;
      row[offset + 3] = alpha_bucket == 0
          ? static_cast<uchar>(0)
          : alpha_bucket == 1
              ? static_cast<uchar>(96)
              : alpha_bucket == 2
                  ? static_cast<uchar>(192)
                  : static_cast<uchar>(255);
    }
  }
  return image;
}

QImage signed_hdr_float_source() {
  constexpr int kWidth = 8;
  constexpr int kHeight = 2;
  auto image = QImage(kWidth, kHeight, QImage::Format_RGBA32FPx4);
  if (image.isNull()) {
    return {};
  }
  image.setColorSpace(
      photo_editor::desktop::linear_srgb_color_space());
  constexpr std::array<ColorPixel, kWidth * kHeight> kPixels = {{
      {.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.0F},
      {.red = 0.18F, .green = 0.18F, .blue = 0.18F, .alpha = 0.25F},
      {.red = 0.8F, .green = 0.2F, .blue = 0.1F, .alpha = 0.5F},
      {.red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = 1.0F},
      {.red = 2.0F, .green = 0.5F, .blue = 0.125F, .alpha = 0.75F},
      {.red = 8.0F, .green = 4.0F, .blue = 2.0F, .alpha = 1.0F},
      {.red = -0.05F, .green = 0.1F, .blue = 0.2F, .alpha = 0.5F},
      {.red = 0.3F, .green = -0.125F, .blue = 0.1F, .alpha = 0.0F},
      {.red = 0.01F, .green = 0.02F, .blue = 0.03F, .alpha = 1.0F},
      {.red = 0.25F, .green = 0.5F, .blue = 0.75F, .alpha = 0.2F},
      {.red = 1.5F, .green = 1.75F, .blue = 2.0F, .alpha = 0.8F},
      {.red = -0.25F, .green = -0.125F, .blue = 0.0F, .alpha = 0.4F},
      {.red = 4.0F, .green = 0.25F, .blue = 0.125F, .alpha = 1.0F},
      {.red = 0.0F, .green = 0.25F, .blue = 0.5F, .alpha = 0.6F},
      {.red = -0.001F, .green = 0.0F, .blue = 0.001F, .alpha = 0.0F},
      {.red = 0.6F, .green = 0.4F, .blue = 0.2F, .alpha = 0.9F},
  }};
  for (auto y = 0; y < kHeight; ++y) {
    auto* row = reinterpret_cast<float*>(image.scanLine(y));
    for (auto x = 0; x < kWidth; ++x) {
      const auto& pixel = kPixels[static_cast<std::size_t>(y * kWidth + x)];
      row[x * 4] = pixel.red;
      row[x * 4 + 1] = pixel.green;
      row[x * 4 + 2] = pixel.blue;
      row[x * 4 + 3] = pixel.alpha;
    }
  }
  return image;
}

EditRecipeState base_legacy_state(const QString& output_profile) {
  auto state = EditRecipeState{};
  state.photographic_rendering_model = QStringLiteral("legacy-oklch-v1");
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = output_profile;
  return state;
}

QByteArray image_pixel_hash(const QImage& image) {
  if (image.isNull() || image.depth() <= 0 || image.depth() % 8 != 0) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto active_row_bytes =
      static_cast<qsizetype>(image.width()) * (image.depth() / 8);
  for (auto y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), active_row_bytes));
  }
  return hash.result().toHex();
}

bool images_have_exact_pixel_storage(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size() ||
      left.format() != right.format() ||
      left.colorSpace() != right.colorSpace() ||
      left.depth() <= 0 || left.depth() % 8 != 0) {
    return false;
  }
  const auto active_row_bytes =
      static_cast<std::size_t>(left.width()) *
      static_cast<std::size_t>(left.depth() / 8);
  for (auto y = 0; y < left.height(); ++y) {
    if (std::memcmp(
            left.constScanLine(y),
            right.constScanLine(y),
            active_row_bytes) != 0) {
      return false;
    }
  }
  return true;
}

double maximum_float_component_delta(
    const QImage& left,
    const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size() ||
      left.format() != QImage::Format_RGBA32FPx4 ||
      right.format() != QImage::Format_RGBA32FPx4) {
    return std::numeric_limits<double>::infinity();
  }
  auto maximum = 0.0F;
  for (auto y = 0; y < left.height(); ++y) {
    const auto* left_row = reinterpret_cast<const float*>(left.constScanLine(y));
    const auto* right_row = reinterpret_cast<const float*>(right.constScanLine(y));
    for (auto x = 0; x < left.width() * 4; ++x) {
      maximum = std::max(maximum, std::abs(left_row[x] - right_row[x]));
    }
  }
  return static_cast<double>(maximum);
}

int maximum_byte_component_delta(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size() ||
      left.format() != right.format() || left.depth() <= 0 ||
      left.depth() % 8 != 0) {
    return std::numeric_limits<int>::max();
  }
  const auto active_row_bytes =
      static_cast<std::size_t>(left.width()) *
      static_cast<std::size_t>(left.depth() / 8);
  auto maximum = 0;
  for (auto y = 0; y < left.height(); ++y) {
    const auto* left_row = reinterpret_cast<const std::uint8_t*>(left.constScanLine(y));
    const auto* right_row = reinterpret_cast<const std::uint8_t*>(right.constScanLine(y));
    for (auto x = std::size_t{0}; x < active_row_bytes; ++x) {
      maximum = std::max(
          maximum,
          std::abs(static_cast<int>(left_row[x]) - static_cast<int>(right_row[x])));
    }
  }
  return maximum;
}

TimedImage render_image(
    const QImage& source,
    const EditRecipeState& state,
    const QColorSpace& target,
    QImage::Format format,
    RenderBackendPreference preference,
    RenderOutputPolicy output_policy = RenderOutputPolicy::DisplayReferred,
    const photo_editor::pipeline::RenderPlan* canonical_plan = nullptr) {
  TimedImage result;
  QElapsedTimer timer;
  timer.start();
  result.image = photo_editor::desktop::render_output_image_with_backend(
      source,
      state,
      target,
      format,
      preference,
      nullptr,
      &result.telemetry,
      {},
      photo_editor::pipeline::RemovalQuality::Final,
      output_policy,
      canonical_plan);
  result.elapsed_ms = static_cast<double>(timer.nsecsElapsed()) / 1.0e6;
  return result;
}

QJsonObject telemetry_json(const RenderBackendTelemetry& telemetry) {
  return QJsonObject{
      {QStringLiteral("backend"), telemetry.backend},
      {QStringLiteral("fallbackReason"), telemetry.fallback_reason},
      {QStringLiteral("canonicalPlanError"), telemetry.canonical_plan_error},
      {QStringLiteral("completedStages"), QJsonArray::fromStringList(
           telemetry.completed_render_stages)},
      {QStringLiteral("renderStageElapsedMs"),
       QJsonObject::fromVariantMap(telemetry.render_stage_elapsed_ms)},
      {QStringLiteral("renderStageBackends"),
       QJsonObject::fromVariantMap(telemetry.render_stage_backends)},
  };
}

bool backend_is_metal(const QString& backend) {
  return backend.startsWith(QStringLiteral("metal"));
}

QJsonArray doubles_json(const std::vector<double>& values) {
  auto array = QJsonArray{};
  for (const auto value : values) {
    array.append(value);
  }
  return array;
}

double median(std::vector<double> values) {
  if (values.empty()) {
    return 0.0;
  }
  std::sort(values.begin(), values.end());
  return values[values.size() / 2U];
}

QJsonObject sample_summary(std::vector<double> values) {
  if (values.empty()) {
    return QJsonObject{{QStringLiteral("meanMs"), 0.0},
                       {QStringLiteral("p95Ms"), 0.0}};
  }
  auto sorted = values;
  std::sort(sorted.begin(), sorted.end());
  const auto p95_index = static_cast<std::size_t>(std::ceil(
      0.95 * static_cast<double>(sorted.size()))) - 1U;
  const auto mean_ms = std::accumulate(values.cbegin(), values.cend(), 0.0) /
      static_cast<double>(values.size());
  return QJsonObject{
      {QStringLiteral("meanMs"), mean_ms},
      {QStringLiteral("p95Ms"), sorted[std::min(p95_index, sorted.size() - 1U)]},
  };
}

struct RenderCase final {
  QString name;
  EditRecipeState state;
  QColorSpace target;
  QImage::Format format = QImage::Format_Invalid;
  RenderOutputPolicy policy = RenderOutputPolicy::DisplayReferred;
  QString expected_fallback;
};

QJsonObject benchmark_render_case(
    const QImage& source,
    const RenderCase& test_case,
    int measured_iterations,
    bool require_gpu_exposure_parity,
    bool* ok) {
  auto row = QJsonObject{
      {QStringLiteral("name"), test_case.name},
      {QStringLiteral("width"), source.width()},
      {QStringLiteral("height"), source.height()},
      {QStringLiteral("format"), static_cast<int>(test_case.format)},
      {QStringLiteral("targetColorSpace"), test_case.target.description()},
      {QStringLiteral("outputPolicy"), test_case.policy == RenderOutputPolicy::SceneLinearWorking
           ? QStringLiteral("scene-linear-working")
           : QStringLiteral("display-referred")},
  };

  const auto cpu_warm = render_image(
      source, test_case.state, test_case.target, test_case.format,
      RenderBackendPreference::CpuOnly, test_case.policy);
  const auto auto_warm = render_image(
      source, test_case.state, test_case.target, test_case.format,
      RenderBackendPreference::Auto, test_case.policy);
  if (cpu_warm.image.isNull() || auto_warm.image.isNull()) {
    *ok = false;
    row.insert(QStringLiteral("error"), QStringLiteral("warmup render failed"));
    row.insert(QStringLiteral("cpuTelemetry"), telemetry_json(cpu_warm.telemetry));
    row.insert(QStringLiteral("autoTelemetry"), telemetry_json(auto_warm.telemetry));
    return row;
  }

  auto cpu_times = std::vector<double>{};
  auto auto_times = std::vector<double>{};
  auto cpu = cpu_warm;
  auto automatic = auto_warm;
  for (auto iteration = 0; iteration < measured_iterations; ++iteration) {
    cpu = render_image(
        source, test_case.state, test_case.target, test_case.format,
        RenderBackendPreference::CpuOnly, test_case.policy);
    automatic = render_image(
        source, test_case.state, test_case.target, test_case.format,
        RenderBackendPreference::Auto, test_case.policy);
    if (cpu.image.isNull() || automatic.image.isNull()) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("measured render failed"));
      break;
    }
    cpu_times.push_back(cpu.elapsed_ms);
    auto_times.push_back(automatic.elapsed_ms);
  }

  const auto exact_pixels = images_have_exact_pixel_storage(cpu.image, automatic.image);
  const auto gpu_only = render_image(
      source, test_case.state, test_case.target, test_case.format,
      RenderBackendPreference::GpuOnly, test_case.policy);
  const auto float_delta = test_case.format == QImage::Format_RGBA32FPx4
      ? maximum_float_component_delta(cpu.image, automatic.image)
      : -1.0;
  const auto actual_backend = automatic.telemetry.backend;
  const auto is_cpu = actual_backend == QStringLiteral("cpu");
  const auto is_metal = backend_is_metal(actual_backend);
  const auto has_expected_fallback =
      test_case.expected_fallback.isEmpty()
          ? automatic.telemetry.fallback_reason.isEmpty()
          : (is_metal
                 ? automatic.telemetry.fallback_reason.isEmpty()
                 : automatic.telemetry.fallback_reason == test_case.expected_fallback);
  const auto protected_fallback = !test_case.expected_fallback.isEmpty();

  if (protected_fallback && !require_gpu_exposure_parity && !is_cpu) {
    *ok = false;
    row.insert(QStringLiteral("error"), QStringLiteral("protected edit did not remain on CPU"));
  }
  if (protected_fallback && !has_expected_fallback) {
    *ok = false;
    row.insert(QStringLiteral("error"), QStringLiteral("protected edit fallback reason changed"));
  }
  if (is_cpu && !exact_pixels) {
    *ok = false;
    row.insert(QStringLiteral("error"), QStringLiteral("CPU fallback output differs from CPU-only golden"));
  }
  if (require_gpu_exposure_parity && is_metal) {
    const auto gpu_only_is_metal = !gpu_only.image.isNull() &&
        backend_is_metal(gpu_only.telemetry.backend) &&
        gpu_only.telemetry.fallback_reason.isEmpty();
    if (!gpu_only_is_metal) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("Auto claimed Metal but GpuOnly did not execute Metal"));
    }
    if (test_case.format == QImage::Format_RGBA32FPx4) {
      if (float_delta > kFloatParityTolerance) {
        *ok = false;
        row.insert(QStringLiteral("error"), QStringLiteral("GPU exposure float parity exceeds strict tolerance"));
      }
    } else if (!exact_pixels) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("GPU exposure output rows differ from CPU golden"));
    }
    if (!images_have_exact_pixel_storage(automatic.image, gpu_only.image)) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("Auto and GpuOnly exposure output rows differ"));
    }
    if (!automatic.telemetry.fallback_reason.isEmpty()) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("Metal backend reported a fallback reason"));
    }
  }
  if (protected_fallback && is_cpu &&
      (!gpu_only.image.isNull() ||
       gpu_only.telemetry.backend != QStringLiteral("none") ||
       gpu_only.telemetry.fallback_reason != test_case.expected_fallback)) {
    *ok = false;
    row.insert(QStringLiteral("error"), QStringLiteral("GpuOnly must reject a protected edit with the same fallback reason"));
  }
  if (test_case.name == QStringLiteral("legacy-neutral-srgb-8bit-protected")) {
    if (!is_metal || gpu_only.image.isNull() ||
        !backend_is_metal(gpu_only.telemetry.backend) ||
        !images_have_exact_pixel_storage(automatic.image, gpu_only.image)) {
      *ok = false;
      row.insert(QStringLiteral("error"), QStringLiteral("neutral Auto/GpuOnly backend telemetry disagrees"));
    }
  }

  row.insert(QStringLiteral("cpuTelemetry"), telemetry_json(cpu.telemetry));
  row.insert(QStringLiteral("autoTelemetry"), telemetry_json(automatic.telemetry));
  row.insert(QStringLiteral("gpuOnlyTelemetry"), telemetry_json(gpu_only.telemetry));
  row.insert(QStringLiteral("gpuOnlyReturnedImage"), !gpu_only.image.isNull());
  row.insert(QStringLiteral("cpuSamplesMs"), doubles_json(cpu_times));
  row.insert(QStringLiteral("autoSamplesMs"), doubles_json(auto_times));
  row.insert(QStringLiteral("cpuMedianMs"), median(cpu_times));
  row.insert(QStringLiteral("autoMedianMs"), median(auto_times));
  row.insert(QStringLiteral("cpuRowsSha256"), QString::fromLatin1(image_pixel_hash(cpu.image)));
  row.insert(QStringLiteral("autoRowsSha256"), QString::fromLatin1(image_pixel_hash(automatic.image)));
  row.insert(QStringLiteral("exactPixelStorage"), exact_pixels);
  row.insert(QStringLiteral("maxByteComponentDelta"),
             maximum_byte_component_delta(cpu.image, automatic.image));
  row.insert(QStringLiteral("maxFloatComponentDelta"), float_delta);
  row.insert(QStringLiteral("gpuOnlyRowsSha256"), QString::fromLatin1(image_pixel_hash(gpu_only.image)));
  row.insert(QStringLiteral("isMetal"), is_metal);
  row.insert(QStringLiteral("expectedFallback"), test_case.expected_fallback);

  if (is_metal && !require_gpu_exposure_parity) {
    // This neutral row is a protected baseline of the existing compatibility
    // route. Its hashes and delta are recorded, but it does not qualify the
    // exact-exposure candidate.
    row.insert(QStringLiteral("qualification"), QStringLiteral("existing compatibility route; diagnostic only"));
  }
  return row;
}

bool run_file_export_case(
    const QString& source_path,
    const QImage& source,
    const EditRecipeState& state,
    const QColorSpace& target,
    QImage::Format format,
    RenderOutputPolicy policy,
    const QString& expected_fallback,
    QJsonObject* output) {
  if (output == nullptr) {
    return false;
  }
  auto result = QJsonObject{
      {QStringLiteral("pathKind"), QStringLiteral("render_output_file; legacy no-plan decode path")},
      {QStringLiteral("sourceFileBytes"), QFileInfo(source_path).size()},
      {QStringLiteral("sourcePixelsSha256"), QString::fromLatin1(image_pixel_hash(source))},
      {QStringLiteral("targetColorSpace"), target.description()},
      {QStringLiteral("format"), static_cast<int>(format)},
  };

  auto cpu = render_image(
      source, state, target, format,
      RenderBackendPreference::CpuOnly, policy);
  if (cpu.image.isNull()) {
    result.insert(QStringLiteral("error"), QStringLiteral("CPU-only in-memory golden failed"));
    *output = std::move(result);
    return false;
  }
  auto times = std::vector<double>{};
  auto rendered = QImage{};
  auto telemetry = RenderBackendTelemetry{};
  for (auto iteration = 0; iteration < kMeasuredIterations; ++iteration) {
    QElapsedTimer timer;
    timer.start();
    rendered = photo_editor::desktop::render_output_file(
        source_path,
        source.size(),
        QRect(QPoint(0, 0), source.size()),
        state,
        target,
        format,
        nullptr,
        &telemetry,
        {},
        photo_editor::pipeline::RemovalQuality::Final,
        policy,
        nullptr,
        nullptr);
    times.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
    if (rendered.isNull()) {
      result.insert(QStringLiteral("error"), QStringLiteral("render_output_file returned an empty image"));
      *output = std::move(result);
      return false;
    }
  }

  const auto exact_cpu_parity = images_have_exact_pixel_storage(cpu.image, rendered);
  const auto cpu_backend = telemetry.backend == QStringLiteral("cpu");
  const auto reason_matches = expected_fallback.isEmpty()
      ? telemetry.fallback_reason.isEmpty()
      : telemetry.fallback_reason == expected_fallback;
  if (!cpu_backend || !exact_cpu_parity || !reason_matches) {
    result.insert(QStringLiteral("error"), QStringLiteral("file export backend, fallback, or pixels differ from the CPU oracle"));
  }
  result.insert(QStringLiteral("renderTelemetry"), telemetry_json(telemetry));
  result.insert(QStringLiteral("renderSamplesMs"), doubles_json(times));
  result.insert(QStringLiteral("renderMedianMs"), median(times));
  result.insert(QStringLiteral("cpuRowsSha256"), QString::fromLatin1(image_pixel_hash(cpu.image)));
  result.insert(QStringLiteral("fileRowsSha256"), QString::fromLatin1(image_pixel_hash(rendered)));
  result.insert(QStringLiteral("exactPixelStorage"), exact_cpu_parity);
  result.insert(QStringLiteral("expectedFallback"), expected_fallback);
  *output = std::move(result);
  return cpu_backend && exact_cpu_parity && reason_matches;
}

bool run_canonical_file_export_case(
    const QString& source_path,
    const QImage& source,
    const EditRecipeState& state,
    const QColorSpace& target,
    QImage::Format format,
    bool require_canonical_metal,
    int measured_iterations,
    bool require_cpu_fallback,
    const QString& expected_cpu_fallback,
    const QString& expected_gpu_only_fallback,
    QJsonObject* output) {
  if (output == nullptr) {
    return false;
  }
  const auto admission = photo_editor::desktop::RenderPlanStore::shared().admit(
      QStringLiteral("export-exposure-gpu-benchmark:canonical"),
      source_path,
      state,
      source.size(),
      photo_editor::pipeline::RenderPurpose::Export,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation);
  auto result = QJsonObject{
      {QStringLiteral("pathKind"), QStringLiteral("render_output_file; canonical export plan")},
      {QStringLiteral("targetColorSpace"), target.description()},
      {QStringLiteral("format"), static_cast<int>(format)},
      {QStringLiteral("canonicalPlanError"), admission.error},
      {QStringLiteral("measuredIterations"), measured_iterations},
      {QStringLiteral("requireCanonicalMetal"), require_canonical_metal},
      {QStringLiteral("protectedCpuFallback"), require_cpu_fallback},
      {QStringLiteral("expectedCpuFallback"), expected_cpu_fallback},
      {QStringLiteral("expectedGpuOnlyFallback"), expected_gpu_only_fallback},
  };
  if (!admission.admitted || !admission.plan.has_value()) {
    result.insert(QStringLiteral("error"), QStringLiteral("canonical export plan admission failed"));
    *output = std::move(result);
    return false;
  }

  const auto render_file = [&]() {
    auto telemetry = RenderBackendTelemetry{};
    QElapsedTimer timer;
    timer.start();
    auto image = photo_editor::desktop::render_output_file(
        source_path,
        source.size(),
        QRect(QPoint(0, 0), source.size()),
        state,
        target,
        format,
        nullptr,
        &telemetry,
        {},
        photo_editor::pipeline::RemovalQuality::Final,
        RenderOutputPolicy::DisplayReferred,
        nullptr,
        &*admission.plan);
    return std::tuple{
        std::move(image),
        std::move(telemetry),
        static_cast<double>(timer.nsecsElapsed()) / 1.0e6};
  };

  // Compile and warm both paths before collecting the seven reported samples.
  auto cpu_warm = render_image(
      source, state, target, format, RenderBackendPreference::CpuOnly,
      RenderOutputPolicy::DisplayReferred, &*admission.plan);
  auto auto_warm = render_image(
      source, state, target, format, RenderBackendPreference::Auto,
      RenderOutputPolicy::DisplayReferred, &*admission.plan);
  auto file_warm = render_file();
  auto gpu_warm = render_image(
      source, state, target, format, RenderBackendPreference::GpuOnly,
      RenderOutputPolicy::DisplayReferred, &*admission.plan);
  if (cpu_warm.image.isNull() || auto_warm.image.isNull() ||
      std::get<0>(file_warm).isNull()) {
    result.insert(QStringLiteral("error"), QStringLiteral("canonical warmup render failed"));
    *output = std::move(result);
    return false;
  }

  auto cpu_times = std::vector<double>{};
  auto auto_times = std::vector<double>{};
  auto file_times = std::vector<double>{};
  auto cpu = cpu_warm;
  auto automatic = auto_warm;
  auto file = std::get<0>(std::move(file_warm));
  auto file_telemetry = std::get<1>(std::move(file_warm));
  auto gpu_only = gpu_warm;
  for (auto iteration = 0; iteration < measured_iterations; ++iteration) {
    cpu = render_image(
        source, state, target, format, RenderBackendPreference::CpuOnly,
        RenderOutputPolicy::DisplayReferred, &*admission.plan);
    automatic = render_image(
        source, state, target, format, RenderBackendPreference::Auto,
        RenderOutputPolicy::DisplayReferred, &*admission.plan);
    auto measured_file = render_file();
    file = std::get<0>(std::move(measured_file));
    file_telemetry = std::get<1>(std::move(measured_file));
    cpu_times.push_back(cpu.elapsed_ms);
    auto_times.push_back(automatic.elapsed_ms);
    file_times.push_back(std::get<2>(measured_file));
    if (cpu.image.isNull() || automatic.image.isNull() || file.isNull()) {
      result.insert(QStringLiteral("error"), QStringLiteral("canonical measured render failed"));
      *output = std::move(result);
      return false;
    }
  }

  auto cpu_telemetry = cpu.telemetry;
  auto auto_telemetry = automatic.telemetry;
  auto gpu_telemetry = gpu_only.telemetry;
  const auto is_metal = backend_is_metal(automatic.telemetry.backend);
  const auto cpu_pixels_exact = images_have_exact_pixel_storage(cpu.image, automatic.image);
  const auto file_pixels_exact = images_have_exact_pixel_storage(cpu.image, file);
  const auto gpu_pixels_exact = !gpu_only.image.isNull() &&
      images_have_exact_pixel_storage(cpu.image, gpu_only.image);
  const auto auto_photo_backend = automatic.telemetry.render_stage_backends
      .value(QStringLiteral("photographic_output")).toString();
  const auto file_photo_backend = file_telemetry.render_stage_backends
      .value(QStringLiteral("photographic_output")).toString();
  const auto auto_photo_stage_is_metal =
      auto_photo_backend == QStringLiteral("metal-photographic-output");
  const auto auto_hybrid_is_truthful =
      automatic.telemetry.backend == QStringLiteral("metal-hybrid-photographic-output") &&
      auto_photo_stage_is_metal &&
      automatic.telemetry.render_stage_backends
          .value(QStringLiteral("tone")).toString() == QStringLiteral("cpu-reference") &&
      automatic.telemetry.fallback_reason.isEmpty();
  const auto automatic_backend_truthful = is_metal
      ? auto_hybrid_is_truthful
      : automatic.telemetry.backend == QStringLiteral("cpu") &&
            !auto_photo_stage_is_metal;
  const auto gpu_backend_truthful =
      gpu_only.image.isNull() &&
      gpu_only.telemetry.backend == QStringLiteral("none") &&
      gpu_only.telemetry.fallback_reason ==
          QStringLiteral("unsupported_stage:Geometry:canonical_cpu_reference_required");
  const auto cpu_fallback_satisfied = !require_cpu_fallback ||
      (!is_metal && automatic.telemetry.backend == QStringLiteral("cpu") &&
       automatic.telemetry.fallback_reason == expected_cpu_fallback &&
       gpu_only.image.isNull() &&
       gpu_only.telemetry.backend == QStringLiteral("none") &&
       gpu_only.telemetry.fallback_reason == expected_gpu_only_fallback);
  const auto file_backend_truthful = file_telemetry.backend == automatic.telemetry.backend &&
      (is_metal
           ? file_telemetry.fallback_reason.isEmpty() &&
                 file_photo_backend == QStringLiteral("metal-photographic-output")
           : file_telemetry.backend == QStringLiteral("cpu") &&
                 file_photo_backend != QStringLiteral("metal-photographic-output"));
  const auto require_metal_satisfied =
      !require_canonical_metal || auto_hybrid_is_truthful;
  if (cpu.image.isNull() || automatic.image.isNull() || file.isNull() ||
      !cpu_pixels_exact || !file_pixels_exact || !gpu_backend_truthful ||
      !automatic_backend_truthful || !file_backend_truthful ||
      !require_metal_satisfied || !cpu_fallback_satisfied ||
      (is_metal && (!cpu_pixels_exact || !file_pixels_exact))) {
    result.insert(QStringLiteral("error"), QStringLiteral("canonical export must truthfully use CPU or exact Metal and match the same-plan CPU pixel/profile/alpha oracle"));
  }

  result.insert(QStringLiteral("cpuTelemetry"), telemetry_json(cpu_telemetry));
  result.insert(QStringLiteral("autoTelemetry"), telemetry_json(auto_telemetry));
  result.insert(QStringLiteral("fileTelemetry"), telemetry_json(file_telemetry));
  result.insert(QStringLiteral("gpuOnlyTelemetry"), telemetry_json(gpu_telemetry));
  result.insert(QStringLiteral("gpuOnlyReturnedImage"), !gpu_only.image.isNull());
  result.insert(QStringLiteral("gpuOnlyCanonicalRejectionReason"),
                gpu_only.telemetry.fallback_reason);
  result.insert(QStringLiteral("autoBackend"), automatic.telemetry.backend);
  result.insert(QStringLiteral("autoIsMetal"), is_metal);
  result.insert(QStringLiteral("autoPhotographicOutputStageBackend"), auto_photo_backend);
  result.insert(QStringLiteral("filePhotographicOutputStageBackend"), file_photo_backend);
  result.insert(QStringLiteral("autoHybridBackendTruthful"), auto_hybrid_is_truthful);
  result.insert(QStringLiteral("cpuSamplesMs"), doubles_json(cpu_times));
  result.insert(QStringLiteral("autoSamplesMs"), doubles_json(auto_times));
  result.insert(QStringLiteral("fileSamplesMs"), doubles_json(file_times));
  result.insert(QStringLiteral("cpuTiming"), sample_summary(cpu_times));
  result.insert(QStringLiteral("autoTiming"), sample_summary(auto_times));
  result.insert(QStringLiteral("fileTiming"), sample_summary(file_times));
  result.insert(QStringLiteral("cpuRowsSha256"), QString::fromLatin1(image_pixel_hash(cpu.image)));
  result.insert(QStringLiteral("autoRowsSha256"), QString::fromLatin1(image_pixel_hash(automatic.image)));
  result.insert(QStringLiteral("fileRowsSha256"), QString::fromLatin1(image_pixel_hash(file)));
  result.insert(QStringLiteral("gpuOnlyRowsSha256"), QString::fromLatin1(image_pixel_hash(gpu_only.image)));
  result.insert(QStringLiteral("exactCpuAutoPixelProfileAlpha"), cpu_pixels_exact);
  result.insert(QStringLiteral("exactCpuFilePixelProfileAlpha"), file_pixels_exact);
  result.insert(QStringLiteral("exactCpuGpuOnlyPixelProfileAlpha"), gpu_pixels_exact);
  result.insert(QStringLiteral("protectedCpuFallbackSatisfied"), cpu_fallback_satisfied);
  result.insert(QStringLiteral("peakRssBytes"), static_cast<double>(peak_rss_bytes()));
  result.insert(QStringLiteral("cpuOutputFormat"), static_cast<int>(cpu.image.format()));
  result.insert(QStringLiteral("fileOutputFormat"), static_cast<int>(file.format()));
  *output = std::move(result);
  return !cpu.image.isNull() && !automatic.image.isNull() && !file.isNull() &&
      cpu_pixels_exact && file_pixels_exact && gpu_backend_truthful &&
      automatic_backend_truthful && file_backend_truthful &&
      require_metal_satisfied && cpu_fallback_satisfied &&
      (!is_metal || (cpu_pixels_exact && file_pixels_exact));
}

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char* argv[]) {
  QCoreApplication app(argc, argv);
  auto arguments = arguments_for(app);
  const auto require_canonical_metal =
      arguments.contains(QStringLiteral("--require-canonical-metal"));
  auto width = kDefaultWidth;
  auto height = kDefaultHeight;
  if (!parse_positive_int(arguments, QStringLiteral("--width"), &width) ||
      !parse_positive_int(arguments, QStringLiteral("--height"), &height) ||
      width < 8 || height < 2 || width > 8192 || height > 8192) {
    std::cerr << "usage: export_exposure_gpu_benchmark_test [--width N] [--height N] [--require-canonical-metal] [--report path]\n";
    return EXIT_FAILURE;
  }

  QTemporaryDir temp_dir;
  if (!temp_dir.isValid()) {
    return fail("could not create temporary export fixture directory");
  }
  const auto source = deterministic_sdr_source(width, height);
  const auto source_path = temp_dir.filePath(QStringLiteral("exposure-source.png"));
  if (source.isNull() || !source.save(source_path, "PNG")) {
    return fail("could not write deterministic PNG source fixture");
  }

  const auto metal_available = photo_editor::desktop::render_backend_available(
      RenderBackendPreference::GpuOnly);
  if (!metal_available) {
    const auto reason = photo_editor::desktop::render_backend_unavailable_reason(
        RenderBackendPreference::GpuOnly);
    std::cout << "SKIP: Metal export benchmark unavailable: "
              << reason.toStdString() << '\n';
    return 77;
  }

  auto report = QJsonObject{
      {QStringLiteral("schema"), QStringLiteral("looksy.export-exposure-gpu-benchmark.v1")},
      {QStringLiteral("qualification"), QStringLiteral("synthetic deterministic PNG; production photo_render_utils renderer; no RAW camera claim")},
      {QStringLiteral("sourceFixture"), QStringLiteral("generated RGBA PNG with alpha 0, partial, and opaque pixels")},
      {QStringLiteral("sourceWidth"), width},
      {QStringLiteral("sourceHeight"), height},
      {QStringLiteral("sourceRowsSha256"), QString::fromLatin1(image_pixel_hash(source))},
      {QStringLiteral("sourceFileBytes"), QFileInfo(source_path).size()},
      {QStringLiteral("measuredIterations"), kMeasuredIterations},
      {QStringLiteral("metalAvailable"), metal_available},
      {QStringLiteral("exposureParityPolicy"), QStringLiteral("if Auto reports Metal for either primary SDR8 format, compare final integer output rows byte-for-byte; protected CPU cases require exact CPU-only row hashes and expected fallback")},
      {QStringLiteral("canonicalExportPolicy"), QStringLiteral("Primary target is canonical SDR 0.75 exposure in ARGB32 and RGBA8888; the accepted route is a CPU Tone plus Metal PhotographicOutput hybrid with exact CPU pixel/profile/alpha parity. GpuOnly intentionally rejects the canonical CPU-reference stages. --require-canonical-metal gates the primary formats and the sampled RGBA8888 exposure range. 16-bit Display P3, scene-linear float, signed HDR, contrast, and curves remain CPU protected cases.")},
      {QStringLiteral("canonicalMetalAcceptanceScope"), QStringLiteral("Requires actual Metal PhotographicOutput stage evidence for .75 ARGB32 and RGBA8888 plus correctness-only RGBA8888 cases at -2, -1, 0, +1.5, and +2 EV; CPU Tone remains exact and GpuOnly retains its canonical Geometry rejection")},
  };

  auto ok = true;
  const auto srgb = QColorSpace(QColorSpace::SRgb);
  const auto display_p3 = QColorSpace(QColorSpace::DisplayP3);
  auto exposure = base_legacy_state(QStringLiteral("sRGB"));
  exposure.exposure = 0.75;
  auto neutral = base_legacy_state(QStringLiteral("sRGB"));
  auto contrast = base_legacy_state(QStringLiteral("sRGB"));
  contrast.contrast = 18.0;
  auto curve = base_legacy_state(QStringLiteral("sRGB"));
  curve.master_curve_points = {
      QVariantMap{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}},
      QVariantMap{{QStringLiteral("x"), 0.5}, {QStringLiteral("y"), 0.62}},
      QVariantMap{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}},
  };

  auto render_cases = QJsonArray{};
  render_cases.append(benchmark_render_case(
      source,
      {QStringLiteral("legacy-exposure-0.75-srgb-8bit"), exposure, srgb,
       QImage::Format_ARGB32, RenderOutputPolicy::DisplayReferred,
       QStringLiteral("unsupported_stage:Tone:exact_global_tone")},
      kMeasuredIterations,
      true,
      &ok));
  exposure.output_color_space = QStringLiteral("Display P3");
  render_cases.append(benchmark_render_case(
      source,
      {QStringLiteral("legacy-exposure-0.75-display-p3-16bit"), exposure,
       display_p3, QImage::Format_RGBA64,
       RenderOutputPolicy::DisplayReferred,
       QStringLiteral("unsupported_stage:Tone:exact_global_tone")},
      kMeasuredIterations,
      true,
      &ok));
  neutral.output_color_space = QStringLiteral("sRGB");
  render_cases.append(benchmark_render_case(
      source,
      {QStringLiteral("legacy-neutral-srgb-8bit-protected"), neutral, srgb,
       QImage::Format_ARGB32, RenderOutputPolicy::DisplayReferred, {}},
      kMeasuredIterations,
      false,
      &ok));
  render_cases.append(benchmark_render_case(
      source,
      {QStringLiteral("legacy-contrast-protected"), contrast, srgb,
       QImage::Format_ARGB32, RenderOutputPolicy::DisplayReferred,
       QStringLiteral("unsupported_stage:Tone:exact_global_tone")},
      kMeasuredIterations,
      false,
      &ok));
  render_cases.append(benchmark_render_case(
      source,
      {QStringLiteral("legacy-master-curve-protected"), curve, srgb,
       QImage::Format_ARGB32, RenderOutputPolicy::DisplayReferred,
       QStringLiteral("unsupported_stage:Tone:exact_global_tone")},
      kMeasuredIterations,
      false,
      &ok));

  auto file_cases = QJsonArray{};
  auto exposure_srgb = base_legacy_state(QStringLiteral("sRGB"));
  exposure_srgb.exposure = 0.75;
  auto file_case = QJsonObject{};
  if (!run_file_export_case(
          source_path, source, exposure_srgb, srgb,
          QImage::Format_ARGB32, RenderOutputPolicy::DisplayReferred,
          QStringLiteral("unsupported_stage:Tone:exact_global_tone"),
          &file_case)) {
    ok = false;
  }
  file_case.insert(QStringLiteral("name"), QStringLiteral("legacy-file-exposure-0.75-srgb-8bit"));
  file_cases.append(file_case);

  auto exposure_p3 = base_legacy_state(QStringLiteral("Display P3"));
  exposure_p3.exposure = 0.75;
  file_case = {};
  if (!run_file_export_case(
          source_path, source, exposure_p3, display_p3,
          QImage::Format_RGBA64, RenderOutputPolicy::DisplayReferred,
          QStringLiteral("unsupported_stage:Tone:exact_global_tone"),
          &file_case)) {
    ok = false;
  }
  file_case.insert(QStringLiteral("name"), QStringLiteral("legacy-file-exposure-0.75-display-p3-16bit"));
  file_cases.append(file_case);

  auto exposure_linear = base_legacy_state(QStringLiteral("Linear sRGB"));
  exposure_linear.exposure = 0.75;
  exposure_linear.output_headroom = 8.0;
  const auto linear_srgb = photo_editor::desktop::linear_srgb_color_space();
  file_case = {};
  if (!run_file_export_case(
          source_path, source, exposure_linear, linear_srgb,
          QImage::Format_RGBA32FPx4, RenderOutputPolicy::SceneLinearWorking,
          QStringLiteral("unsupported_stage:Tone:exact_global_tone"),
          &file_case)) {
    ok = false;
  }
  file_case.insert(QStringLiteral("name"), QStringLiteral("legacy-file-exposure-0.75-scene-linear-float-hdr-output"));
  file_cases.append(file_case);

  const auto signed_hdr = signed_hdr_float_source();
  if (signed_hdr.isNull()) {
    return fail("could not create signed HDR float fixture");
  }
  auto signed_state = base_legacy_state(QStringLiteral("Linear sRGB"));
  signed_state.exposure = 0.75;
  signed_state.output_headroom = 8.0;
  const auto signed_hdr_case = RenderCase{
      QStringLiteral("legacy-signed-hdr-alpha-exposure-0.75-scene-linear-float"),
      signed_state,
      linear_srgb,
      QImage::Format_RGBA32FPx4,
      RenderOutputPolicy::SceneLinearWorking,
      QStringLiteral("unsupported_stage:SceneAdjustments:extended_range_pixels"),
  };
  const auto signed_full_renderer = benchmark_render_case(
      signed_hdr,
      signed_hdr_case,
      kMeasuredIterations,
      false,
      &ok);
  render_cases.append(signed_full_renderer);
  const auto signed_full_renderer_exact_cpu_auto =
      signed_full_renderer.value(QStringLiteral("exactPixelStorage")).toBool();
  report.insert(
      QStringLiteral("signedHdrFullRendererCpuRowsSha256"),
      signed_full_renderer.value(QStringLiteral("cpuRowsSha256")));
  report.insert(
      QStringLiteral("signedHdrFullRendererAutoRowsSha256"),
      signed_full_renderer.value(QStringLiteral("autoRowsSha256")));
  report.insert(
      QStringLiteral("signedHdrFullRendererExactCpuAuto"),
      signed_full_renderer_exact_cpu_auto);
  if (!signed_full_renderer_exact_cpu_auto) {
    ok = false;
  }

  // This isolated oracle checks the actual CPU SceneAdjustments stage against
  // bounded scalar exposure math. The full renderer above has additional
  // policy stages and is independently required to match its CPU-only output.
  auto signed_stage_input = photo_editor::desktop::working_image_from_qimage(
      signed_hdr, signed_state.working_color_profile);
  auto signed_stage_actual = signed_stage_input.pixels;
  auto signed_stage_reference = signed_stage_input.pixels;
  const auto signed_stage_state =
      photo_editor::desktop::compile_base_color_adjustment_state(signed_state);
  const auto signed_stage_applied = !signed_stage_input.isNull() &&
      photo_editor::pipeline::apply_color_scene_adjustments_stage(
          &signed_stage_actual,
          signed_stage_input.width,
          signed_stage_input.height,
          signed_stage_state);
  if (!signed_stage_applied ||
      signed_stage_reference.size() != signed_stage_actual.size()) {
    ok = false;
  } else {
    const auto expected_gain = std::exp2(signed_state.exposure);
    auto maximum_error = 0.0F;
    auto alpha_exact = true;
    for (std::size_t index = 0; index < signed_stage_actual.size(); ++index) {
      auto& expected = signed_stage_reference[index];
      expected.red = static_cast<float>(static_cast<double>(expected.red) * expected_gain);
      expected.green = static_cast<float>(static_cast<double>(expected.green) * expected_gain);
      expected.blue = static_cast<float>(static_cast<double>(expected.blue) * expected_gain);
      const auto& actual = signed_stage_actual[index];
      maximum_error = std::max(maximum_error, std::abs(actual.red - expected.red));
      maximum_error = std::max(maximum_error, std::abs(actual.green - expected.green));
      maximum_error = std::max(maximum_error, std::abs(actual.blue - expected.blue));
      maximum_error = std::max(maximum_error, std::abs(actual.alpha - expected.alpha));
      alpha_exact = alpha_exact && actual.alpha == signed_stage_input.pixels[index].alpha;
    }
    report.insert(QStringLiteral("signedHdrLowLevelSceneExposureGoldenMaxError"),
                  static_cast<double>(maximum_error));
    report.insert(QStringLiteral("signedHdrLowLevelSceneExposureAlphaExact"), alpha_exact);
    if (maximum_error > kFloatParityTolerance || !alpha_exact) {
      ok = false;
    }
  }

  auto canonical_state = EditRecipeState{};
  canonical_state.working_color_profile = QStringLiteral("Linear sRGB");
  canonical_state.preview_color_space = QStringLiteral("sRGB");
  canonical_state.output_color_space = QStringLiteral("sRGB");
  canonical_state.exposure = 0.75;
  const auto append_canonical_case = [&](const QString& name,
                                         const EditRecipeState& state,
                                         const QColorSpace& target,
                                         QImage::Format format,
                                         bool require_metal,
                                         int measured_iterations,
                                         bool require_cpu,
                                         const QString& expected_cpu_fallback,
                                         const QString& expected_gpu_only_fallback) {
    auto canonical_case = QJsonObject{};
    if (!run_canonical_file_export_case(
            source_path,
            source,
            state,
            target,
            format,
            require_metal,
            measured_iterations,
            require_cpu,
            expected_cpu_fallback,
            expected_gpu_only_fallback,
            &canonical_case)) {
      ok = false;
    }
    canonical_case.insert(QStringLiteral("name"), name);
    file_cases.append(canonical_case);
  };
  append_canonical_case(
      QStringLiteral("canonical-file-export-exposure-0.75-srgb-8bit-argb32"),
      canonical_state,
      srgb,
      QImage::Format_ARGB32,
      require_canonical_metal,
      kCanonicalMeasuredIterations,
      false,
      {},
      {});
  append_canonical_case(
      QStringLiteral("canonical-file-export-exposure-0.75-srgb-8bit-rgba8888"),
      canonical_state,
      srgb,
      QImage::Format_RGBA8888,
      require_canonical_metal,
      kCanonicalMeasuredIterations,
      false,
      {},
      {});
  auto canonical_p3_state = canonical_state;
  canonical_p3_state.output_color_space = QStringLiteral("Display P3");
  append_canonical_case(
      QStringLiteral("canonical-file-export-exposure-0.75-display-p3-16bit-protected"),
      canonical_p3_state,
      display_p3,
      QImage::Format_RGBA64,
      false,
      1,
      true,
      QStringLiteral("unsupported_stage:Tone:exact_global_tone"),
      QStringLiteral("unsupported_stage:Geometry:canonical_cpu_reference_required"));
  for (const auto exposure_value : {-2.0, -1.0, 0.0, 1.5, 2.0}) {
    auto exposure_variant = canonical_state;
    exposure_variant.exposure = exposure_value;
    append_canonical_case(
        QStringLiteral("canonical-file-export-exposure-%1-srgb-8bit-rgba8888-correctness")
            .arg(exposure_value, 0, 'f', 1),
        exposure_variant,
        srgb,
        QImage::Format_RGBA8888,
        require_canonical_metal,
        1,
        false,
        {},
        {});
  }

  report.insert(QStringLiteral("renderCases"), render_cases);
  report.insert(QStringLiteral("fileCases"), file_cases);
  report.insert(QStringLiteral("peakRssBytes"), static_cast<double>(peak_rss_bytes()));
  report.insert(QStringLiteral("requireCanonicalMetal"), require_canonical_metal);
  report.insert(QStringLiteral("result"), ok ? QStringLiteral("pass") : QStringLiteral("fail"));
  const auto report_bytes = QJsonDocument(report).toJson(QJsonDocument::Indented);
  const auto report_path = report_path_for(arguments);
  if (!report_path.isEmpty()) {
    QFile report_file(report_path);
    if (!report_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        report_file.write(report_bytes) != report_bytes.size()) {
      return fail("could not write benchmark report");
    }
  }
  std::cout << report_bytes.constData() << '\n';
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
