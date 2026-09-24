#include <QByteArrayView>
#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSysInfo>
#include <QThread>
#include <CoreFoundation/CoreFoundation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "photo_editor/pipeline/color_adjustments.h"
#include "photo_editor/pipeline/color_metrics.h"
#include "photo_editor/pipeline/color_pipeline.h"
#include "photo_editor/pipeline/color_transform.h"
#include "canonical_native_working_fusion.h"
#include "editor_preview_texture.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

namespace {

[[nodiscard]] double percentile(
    const std::vector<double>& sorted_values,
    double fraction) {
  if (sorted_values.empty()) {
    return 0.0;
  }
  const auto rank = static_cast<std::size_t>(std::ceil(
      std::clamp(fraction, 0.0, 1.0) *
      static_cast<double>(sorted_values.size())));
  return sorted_values[
      std::min(
          sorted_values.size() - 1U,
          std::max<std::size_t>(1U, rank) - 1U)];
}

struct ParityMetrics final {
  std::size_t pixel_count = 0U;
  double p99_delta_e_00 = 0.0;
  double maximum_delta_e_00 = 0.0;
  double maximum_channel_error = 0.0;
};

[[nodiscard]] bool measure_parity(
    const std::vector<photo_editor::pipeline::ColorPixel>& expected,
    const std::vector<photo_editor::pipeline::ColorPixel>& actual,
    const photo_editor::pipeline::RgbColorSpace& working_space,
    ParityMetrics* metrics) {
  if (metrics == nullptr || expected.empty() || expected.size() != actual.size()) {
    return false;
  }
  std::vector<double> errors;
  errors.reserve(expected.size());
  auto maximum_channel_error = 0.0;
  for (std::size_t index = 0U; index < expected.size(); ++index) {
    const auto& expected_pixel = expected[index];
    const auto& actual_pixel = actual[index];
    if (!std::isfinite(actual_pixel.red) || !std::isfinite(actual_pixel.green) ||
        !std::isfinite(actual_pixel.blue) || !std::isfinite(actual_pixel.alpha) ||
        std::abs(actual_pixel.alpha - expected_pixel.alpha) > 1.0e-6F) {
      return false;
    }
    maximum_channel_error = std::max({
        maximum_channel_error,
        std::abs(static_cast<double>(actual_pixel.red - expected_pixel.red)),
        std::abs(static_cast<double>(actual_pixel.green - expected_pixel.green)),
        std::abs(static_cast<double>(actual_pixel.blue - expected_pixel.blue)),
    });
    errors.push_back(photo_editor::pipeline::perceptual_delta(
        expected_pixel, actual_pixel, working_space));
  }
  std::sort(errors.begin(), errors.end());
  *metrics = ParityMetrics{
      .pixel_count = errors.size(),
      .p99_delta_e_00 = percentile(errors, 0.99),
      .maximum_delta_e_00 = errors.back(),
      .maximum_channel_error = maximum_channel_error,
  };
  return true;
}

[[nodiscard]] bool parity_passes(const ParityMetrics& metrics) {
  return metrics.p99_delta_e_00 <= 0.25 &&
         metrics.maximum_delta_e_00 <= 0.75 &&
         metrics.maximum_channel_error <= 2.0e-4;
}

[[nodiscard]] std::uint8_t srgb8(double linear) {
  const auto bounded = std::clamp(linear, 0.0, 1.0);
  const auto encoded = bounded <= 0.0031308
                           ? 12.92 * bounded
                           : 1.055 * std::pow(bounded, 1.0 / 2.4) - 0.055;
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(encoded * 255.0), 0L, 255L));
}

[[nodiscard]] std::uint8_t unit8(double value) {
  return static_cast<std::uint8_t>(
      std::clamp(std::lround(std::clamp(value, 0.0, 1.0) * 255.0), 0L, 255L));
}

[[nodiscard]] QJsonObject parity_contract(const ParityMetrics& metrics) {
  return {
      {QStringLiteral("pixel_count"), static_cast<qint64>(metrics.pixel_count)},
      {QStringLiteral("p99_delta_e_00"), metrics.p99_delta_e_00},
      {QStringLiteral("maximum_delta_e_00"), metrics.maximum_delta_e_00},
      {QStringLiteral("maximum_channel_error"), metrics.maximum_channel_error},
      {QStringLiteral("p99_gate"), 0.25},
      {QStringLiteral("maximum_gate"), 0.75},
      {QStringLiteral("channel_gate"), 2.0e-4},
  };
}

[[nodiscard]] QByteArray sha256_file(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  while (!file.atEnd()) {
    const auto block = file.read(1024 * 1024);
    if (block.isEmpty() && !file.atEnd()) {
      return {};
    }
    hash.addData(block);
  }
  return hash.result().toHex();
}

[[nodiscard]] QByteArray pixel_sha256(const QImage& source) {
  const auto image = source.convertToFormat(QImage::Format_RGBA8888);
  if (image.isNull()) {
    return {};
  }
  QCryptographicHash hash(QCryptographicHash::Sha256);
  const auto row_bytes = static_cast<qsizetype>(image.width()) * 4;
  for (int y = 0; y < image.height(); ++y) {
    hash.addData(QByteArrayView(
        reinterpret_cast<const char*>(image.constScanLine(y)), row_bytes));
  }
  return hash.result().toHex();
}

[[nodiscard]] bool valid_reference_id(const QString& value) {
  if (!value.startsWith(QStringLiteral("references/checker24/")) ||
      value.startsWith(QLatin1Char('/')) || value.contains(QLatin1Char('\\')) ||
      value.contains(QLatin1Char(':'))) {
    return false;
  }
  const auto parts = value.split(QLatin1Char('/'), Qt::KeepEmptyParts);
  return std::none_of(parts.cbegin(), parts.cend(), [](const QString& part) {
    return part.isEmpty() || part == QStringLiteral(".") ||
           part == QStringLiteral("..");
  });
}

struct Arguments final {
  QString tagged_set_path;
  QString report_path;
  bool valid = true;
};

[[nodiscard]] Arguments parse_arguments(int argc, char* argv[]) {
  auto arguments = Arguments{};
  for (int index = 1; index < argc; index += 2) {
    if (index + 1 >= argc) {
      arguments.valid = false;
      break;
    }
    const auto name = QString::fromLocal8Bit(argv[index]);
    const auto value = QString::fromLocal8Bit(argv[index + 1]);
    if (value.isEmpty() ||
        (name == QStringLiteral("--tagged-set") &&
         !arguments.tagged_set_path.isEmpty()) ||
        (name == QStringLiteral("--report") &&
         !arguments.report_path.isEmpty())) {
      arguments.valid = false;
      break;
    }
    if (name == QStringLiteral("--tagged-set")) {
      arguments.tagged_set_path = value;
    } else if (name == QStringLiteral("--report")) {
      arguments.report_path = value;
    } else {
      arguments.valid = false;
      break;
    }
  }
  return arguments;
}

[[nodiscard]] bool load_tagged_reference_pixels(
    const QJsonObject& reference,
    const QDir& artifact_root,
    const QByteArray& expected_icc_sha256,
    const photo_editor::pipeline::ColorMatrix3& linear_srgb_to_working,
    std::vector<photo_editor::pipeline::ColorPixel>* pixels,
    std::string* failure_reason) {
  if (pixels == nullptr || failure_reason == nullptr) {
    return false;
  }
  const auto output_id = reference.value(QStringLiteral("output_id")).toString();
  const auto expected_file_sha256 =
      reference.value(QStringLiteral("output_sha256")).toString().toLatin1();
  const auto expected_pixel_sha256 =
      reference.value(QStringLiteral("output_pixel_sha256")).toString().toLatin1();
  if (!valid_reference_id(output_id) || expected_file_sha256.size() != 64 ||
      expected_pixel_sha256.size() != 64) {
    *failure_reason = "invalid tagged-reference identity or digest";
    return false;
  }
  const auto path = artifact_root.absoluteFilePath(output_id);
  if (sha256_file(path) != expected_file_sha256) {
    *failure_reason = "tagged-reference file digest mismatch: " +
                      output_id.toStdString();
    return false;
  }
  QImageReader reader(path, "png");
  reader.setAutoTransform(false);
  const auto image = reader.read();
  if (image.isNull() || image.width() != 606 || image.height() != 404 ||
      image.colorSpace().primaries() != QColorSpace::Primaries::SRgb ||
      image.colorSpace().transferFunction() !=
          QColorSpace::TransferFunction::Linear ||
      pixel_sha256(image) != expected_pixel_sha256 ||
      QCryptographicHash::hash(
          image.colorSpace().iccProfile(), QCryptographicHash::Sha256).toHex() !=
          expected_icc_sha256) {
    *failure_reason = "tagged-reference pixel/profile contract mismatch: " +
                      output_id.toStdString();
    return false;
  }
  const auto rgba = image.convertToFormat(QImage::Format_RGBA8888);
  pixels->clear();
  pixels->reserve(static_cast<std::size_t>(rgba.width() * rgba.height()));
  constexpr auto kByteToUnit = 1.0 / 255.0;
  for (int y = 0; y < rgba.height(); ++y) {
    const auto* row = rgba.constScanLine(y);
    for (int x = 0; x < rgba.width(); ++x) {
      const auto offset = static_cast<qsizetype>(x) * 4;
      const auto working = photo_editor::pipeline::multiply_matrix3_vector(
          linear_srgb_to_working,
          {
              static_cast<double>(row[offset]) * kByteToUnit,
              static_cast<double>(row[offset + 1]) * kByteToUnit,
              static_cast<double>(row[offset + 2]) * kByteToUnit,
          });
      pixels->push_back(photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(working[0]),
          .green = static_cast<float>(working[1]),
          .blue = static_cast<float>(working[2]),
          .alpha = static_cast<float>(
              static_cast<double>(row[offset + 3]) * kByteToUnit),
      });
    }
  }
  return true;
}

}  // namespace

int main(int argc, char* argv[]) {
  const auto arguments = parse_arguments(argc, argv);
  const auto& tagged_set_path = arguments.tagged_set_path;
  if (!arguments.valid ||
      (!arguments.report_path.isEmpty() && tagged_set_path.isEmpty()) ||
      (!arguments.report_path.isEmpty() && QFileInfo::exists(arguments.report_path))) {
    std::cerr << "usage: metal_aces2_output_lut_parity_test "
                 "[--tagged-set manifest.json] [--report report.json]\n";
    return EXIT_FAILURE;
  }
  QCoreApplication qt_app(argc, argv);
  static_cast<void>(qt_app);
  photo_editor::pipeline::metal::MetalPipelineBackend metal;
  if (!metal.available()) {
    const auto reason = metal.unavailable_reason();
    if (reason == "no_system_metal_device") {
      std::cout << "SKIP: no system Metal device is available\n";
      return 77;
    }
    std::cerr << "Metal ACES 2 output backend failed initialization: "
              << reason << '\n';
    return EXIT_FAILURE;
  }
  {
    auto texture = photo_editor::desktop::EditorPreviewTexture::create();
    if (!texture || !texture->ensureNativeTexture(QSize(4, 4), QImage::Format_ARGB32))
      return EXIT_FAILURE;
    const auto handle = texture->nativeTextureHandle();
    const auto native = reinterpret_cast<CFTypeRef>(static_cast<std::uintptr_t>(handle));
    const auto retain_count = CFGetRetainCount(native);
    auto lease = texture->nativeTextureLease();
    if (!lease || reinterpret_cast<std::uintptr_t>(lease.get()) != handle)
      return EXIT_FAILURE;
    if (CFGetRetainCount(native) <= retain_count) {
      std::cerr << "The native lease must retain the Metal resource, not just its address\n";
      return EXIT_FAILURE;
    }
    std::weak_ptr<void> lifetime = lease;
    texture->clear();
    texture.reset();
    if (lifetime.expired()) return EXIT_FAILURE;
    lease.reset();
    if (!lifetime.expired()) return EXIT_FAILURE;
  }
#ifdef NDEBUG
  constexpr auto kBuildMode = "release";
#else
  constexpr auto kBuildMode = "debug";
#endif
  if (!arguments.report_path.isEmpty() &&
      std::string_view(kBuildMode) != std::string_view("release")) {
    std::cerr << "physical Metal proof reports require a Release build\n";
    return EXIT_FAILURE;
  }

  auto config = photo_editor::pipeline::default_color_pipeline_config();
  config.photographic_rendering_model =
      photo_editor::pipeline::PhotographicRenderingModel::Aces2OcioV1;
  config.black_point_compensation_enabled = true;
  config.source_black_point = 0.02;
  config.output_black_point = 0.005;
  const auto application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          photo_editor::pipeline::Aces2OutputLutQuality::Final);
  const auto interactive_application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          photo_editor::pipeline::Aces2OutputLutQuality::Interactive);
  if (!application.ready() || !interactive_application.ready()) {
    std::cout << "SKIP: official ACES 2 output LUT is unavailable\n";
    return 77;
  }

  constexpr int kEdge = 13;
  std::vector<photo_editor::pipeline::ColorPixel> source;
  source.reserve(static_cast<std::size_t>(kEdge * kEdge));
  for (int y = 0; y < kEdge; ++y) {
    const auto yn = static_cast<double>(y) /
                    static_cast<double>(kEdge - 1);
    for (int x = 0; x < kEdge; ++x) {
      const auto xn = static_cast<double>(x) /
                      static_cast<double>(kEdge - 1);
      const auto exposure = std::exp2(10.0 * xn) - 1.0;
      source.push_back(photo_editor::pipeline::ColorPixel{
          .red = static_cast<float>(exposure * (1.2 * yn - 0.1)),
          .green = static_cast<float>(exposure * (0.8 * (1.0 - yn))),
          .blue = static_cast<float>(exposure * (0.05 + 0.7 *
                                                 std::abs(yn - 0.5))),
          .alpha = static_cast<float>(-0.25 + 1.5 * xn),
      });
    }
  }
  auto cpu = source;
  for (std::size_t index = 0U; index < cpu.size(); ++index) {
    cpu[index] = photo_editor::pipeline::apply_aces2_output_lut_target_application(
        application,
        source[index]);
  }
  auto gpu = source;
  photo_editor::pipeline::ColorAdjustmentTelemetry telemetry;
  if (!metal.apply_aces2_output_lut_target(
          &gpu,
          kEdge,
          kEdge,
          application,
          &telemetry)) {
    std::cerr << "Metal ACES 2 output LUT dispatch or readback failed\n";
    return EXIT_FAILURE;
  }
  ParityMetrics final_metrics;
  if (!measure_parity(
          cpu, gpu, config.preview_display_target.color_space, &final_metrics) ||
      !parity_passes(final_metrics)) {
    std::cerr << "Metal ACES 2 97^3 output LUT exceeds the CPU/GPU parity gate\n";
    return EXIT_FAILURE;
  }
  const auto pixel_bytes = static_cast<std::int64_t>(
      gpu.size() * sizeof(photo_editor::pipeline::ColorPixel));
  if (telemetry.gpu_command_buffer_count != 1 ||
      telemetry.gpu_kernel_dispatch_count != 1 ||
      telemetry.gpu_cpu_to_gpu_bytes < pixel_bytes ||
      telemetry.gpu_gpu_to_cpu_bytes != pixel_bytes) {
    std::cerr << "Metal ACES 2 output LUT must report dispatch and transfer telemetry\n";
    return EXIT_FAILURE;
  }
  auto surface_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto surface = metal.publish_aces2_output_lut_srgb8_surface(
      source, kEdge, kEdge, application, &surface_telemetry);
  std::vector<std::uint8_t> surface_pixels(source.size() * 4U);
  if (surface == nullptr || surface->width() != kEdge ||
      surface->height() != kEdge || surface->native_texture_handle() == 0U ||
      surface->device_registry_id() == 0U ||
      !surface->copy_bgra8_pixels_for_testing(surface_pixels) ||
      surface_telemetry.gpu_command_buffer_count != 1 ||
      surface_telemetry.gpu_kernel_dispatch_count != 1 ||
      surface_telemetry.gpu_cpu_to_gpu_bytes < pixel_bytes ||
      surface_telemetry.gpu_gpu_to_cpu_bytes != 0) {
    std::cerr << "native sRGB output surface must publish without production readback\n";
    return EXIT_FAILURE;
  }
  auto preview_texture = photo_editor::desktop::EditorPreviewTexture::create();
  if (preview_texture == nullptr ||
      !preview_texture->adoptNativeSurface(*surface) ||
      preview_texture->nativeTextureHandle() != surface->native_texture_handle() ||
      preview_texture->nativeTextureSize() != QSize(kEdge, kEdge)) {
    std::cerr << "preview texture must directly adopt the retained full-frame surface\n";
    return EXIT_FAILURE;
  }
  const auto adopted_handle = preview_texture->nativeTextureHandle();
  auto async_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto async_surface = metal.publish_aces2_output_lut_srgb8_surface(
      std::span<const photo_editor::pipeline::ColorPixel>(source).first(9U),
      3,
      3,
      application,
      &async_telemetry,
      {},
      true);
  auto async_finished = std::atomic_bool{false};
  auto async_published = std::atomic_bool{false};
  if (async_surface == nullptr || async_surface->native_ready_event_handle() == 0U ||
      async_surface->ready_event_value() == 0U ||
      !preview_texture->uploadNativePatchAsync(
          *async_surface,
          QRect(2, 2, 3, 3),
          []() { return true; },
          [&](bool published) {
            async_published.store(published);
            async_finished.store(true);
          })) {
    std::cerr << "deferred Metal surface must schedule a readiness-fenced patch\n";
    std::cerr << "surface=" << (async_surface != nullptr)
              << " ready_event=" << (async_surface != nullptr ? async_surface->native_ready_event_handle() : 0U)
              << " ready_value=" << (async_surface != nullptr ? async_surface->ready_event_value() : 0U)
              << " supports_native=" << preview_texture->supportsNativeOutputSurface() << '\n';
    return EXIT_FAILURE;
  }
  QElapsedTimer async_deadline;
  async_deadline.start();
  while (!async_finished.load() && async_deadline.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  if (!async_finished.load() || !async_published.load() ||
      !async_surface->producer_completed_successfully() ||
      preview_texture->nativeTextureHandle() == adopted_handle) {
    std::cerr << "readiness-fenced patch must atomically replace the preview texture\n";
    return EXIT_FAILURE;
  }
  const auto patched_handle = preview_texture->nativeTextureHandle();
  const auto deferred_full_surface = metal.publish_aces2_output_lut_srgb8_surface(
      source, kEdge, kEdge, application, nullptr, {}, true);
  auto full_finished = std::atomic_bool{false};
  auto full_published = std::atomic_bool{false};
  if (deferred_full_surface == nullptr ||
      deferred_full_surface->native_ready_event_handle() == 0U ||
      !preview_texture->adoptNativeSurfaceAsync(
          *deferred_full_surface,
          [&]() { return deferred_full_surface->producer_completed_successfully(); },
          [&](bool published) {
            full_published.store(published);
            full_finished.store(true);
          })) {
    std::cerr << "deferred full-frame surface must schedule readiness-fenced adoption\n";
    return EXIT_FAILURE;
  }
  async_deadline.restart();
  while (!full_finished.load() && async_deadline.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  if (!full_finished.load() || !full_published.load() ||
      preview_texture->nativeTextureHandle() !=
          deferred_full_surface->native_texture_handle() ||
      preview_texture->nativeTextureHandle() == patched_handle) {
    std::cerr << "readiness-fenced full-frame adoption must publish the producer texture\n";
    return EXIT_FAILURE;
  }
  const auto full_handle = preview_texture->nativeTextureHandle();
  const auto stale_full_surface = metal.publish_aces2_output_lut_srgb8_surface(
      source, kEdge, kEdge, application, nullptr, {}, true);
  auto stale_finished = std::atomic_bool{false};
  auto stale_published = std::atomic_bool{true};
  if (stale_full_surface == nullptr ||
      !preview_texture->adoptNativeSurfaceAsync(
          *stale_full_surface,
          []() { return false; },
          [&](bool published) {
            stale_published.store(published);
            stale_finished.store(true);
          })) {
    std::cerr << "stale full-frame surface must schedule before commit rejection\n";
    return EXIT_FAILURE;
  }
  async_deadline.restart();
  while (!stale_finished.load() && async_deadline.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  if (!stale_finished.load() || stale_published.load() ||
      preview_texture->nativeTextureHandle() != full_handle) {
    std::cerr << "stale full-frame adoption must preserve the last complete texture\n";
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0U; index < cpu.size(); ++index) {
    const auto offset = index * 4U;
    const auto expected = std::array<std::uint8_t, 4>{
        srgb8(cpu[index].blue),
        srgb8(cpu[index].green),
        srgb8(cpu[index].red),
        unit8(cpu[index].alpha),
    };
    for (std::size_t channel = 0U; channel < expected.size(); ++channel) {
      const auto delta = std::abs(
          static_cast<int>(surface_pixels[offset + channel]) -
          static_cast<int>(expected[channel]));
      if (delta > 1) {
        std::cerr << "native sRGB output surface exceeded one-code-value parity\n";
        return EXIT_FAILURE;
      }
    }
  }
  auto fused_state =
      photo_editor::pipeline::ColorAdjustmentState{};
  fused_state.saturation = 0.35;
  fused_state.linear_mask_enabled = true;
  fused_state.linear_mask_exposure = -0.2;
  fused_state.linear_mask_temperature = 0.1;
  auto fused_source = source;
  const auto fused_lut =
      photo_editor::pipeline::build_color_lut(
          fused_state);
  auto fused_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto fused_output_changed = [&]() {
    for (std::size_t index = 0U;
         index < fused_source.size();
         ++index) {
      const auto& left = fused_source[index];
      const auto& right = source[index];
      if (left.red != right.red ||
          left.green != right.green ||
          left.blue != right.blue ||
          left.alpha != right.alpha) {
        return true;
      }
    }
    return false;
  };
  if (!metal.apply_masks_and_color_lut(
          &fused_source,
          kEdge,
          kEdge,
          fused_state,
          fused_lut,
          &fused_telemetry) ||
      fused_telemetry.gpu_command_buffer_count != 1 ||
      fused_telemetry.gpu_kernel_dispatch_count != 2 ||
      fused_telemetry.gpu_cpu_to_gpu_bytes !=
          pixel_bytes +
              static_cast<std::int64_t>(
                  fused_lut.table.size() *
                  sizeof(photo_editor::pipeline::ColorPixel)) ||
      fused_telemetry.gpu_gpu_to_cpu_bytes !=
          pixel_bytes ||
      !fused_output_changed()) {
    std::cerr
        << "fused Metal mask plus color LUT must use one command buffer, "
           "two dispatches, one upload, and one readback\n";
    return EXIT_FAILURE;
  }
  const auto fused_upload_before_reuse =
      fused_telemetry.gpu_cpu_to_gpu_bytes;
  fused_source = source;
  if (!metal.apply_masks_and_color_lut(
          &fused_source,
          kEdge,
          kEdge,
          fused_state,
          fused_lut,
          &fused_telemetry) ||
      fused_telemetry.gpu_command_buffer_count != 2 ||
      fused_telemetry.gpu_kernel_dispatch_count != 4 ||
      fused_telemetry.gpu_cpu_to_gpu_bytes -
              fused_upload_before_reuse !=
          pixel_bytes ||
      fused_telemetry.gpu_gpu_to_cpu_bytes !=
          pixel_bytes * 2) {
    std::cerr
        << "a reused fused Metal color LUT must upload only the changed pixel surface\n";
    return EXIT_FAILURE;
  }
  const auto fused_lut_cache =
      metal.color_lut_cache_stats();
  if (fused_lut_cache.entries != 1U ||
      fused_lut_cache.bytes !=
          fused_lut.table.size() *
              sizeof(
                  photo_editor::pipeline::
                      ColorPixel) ||
      fused_lut_cache.bytes >
          fused_lut_cache.maximum_bytes ||
      fused_lut_cache.entries >
          fused_lut_cache.maximum_entries) {
    std::cerr
        << "the fused Metal color LUT cache must expose and honor its aggregate ceilings\n";
    return EXIT_FAILURE;
  }
  const auto working_descriptor = photo_editor::pipeline::RenderSurfaceDescriptor{
      .pixel_encoding = photo_editor::pipeline::RenderPixelEncoding::SceneLinearFloat,
      .color_domain = photo_editor::pipeline::RenderColorDomain::WorkingScene,
      .range = photo_editor::pipeline::RenderRangeModel::SceneExtended,
      .alpha = photo_editor::pipeline::RenderAlphaModel::Straight,
      .color_space_identity = "Linear ProPhoto RGB",
      .transfer_identity = "linear",
      .width = static_cast<std::uint32_t>(kEdge),
      .height = static_cast<std::uint32_t>(kEdge),
  };
  // An SDR graph must not clamp the extended-range working image. Exercise
  // both the global curve shader and the retained adjustment-layer shader.
  {
    using namespace photo_editor::pipeline;
    auto curve_metal = metal::MetalPipelineBackend{};
    const std::vector<ColorPixel> hdr_source{
        {0.18F, 0.32F, 0.6F, 1.0F}, {2.0F, 3.0F, 4.0F, 0.5F},
        {4.0F, 6.0F, 8.0F, 1.0F}};
    for (int variant = 0; variant < 6; ++variant) {
      ColorAdjustmentState state;
      state.color_pipeline = default_color_pipeline_config();
      const std::vector<CurvePoint> identity{{0, 0}, {0.5, 0.5}, {1, 1}};
      if (variant == 0) state.master_curve_points = identity;
      if (variant == 1) state.luma_curve_points = identity;
      if (variant == 2) state.red_curve_points = identity;
      if (variant == 3) state.green_curve_points = identity;
      if (variant == 4) state.blue_curve_points = identity;
      if (variant == 5) {
        PhotoLayer layer;
        layer.kind = "adjustment";
        layer.master_curve_points = identity;
        state.photo_layers.push_back(layer);
      }
      auto descriptor = working_descriptor;
      descriptor.width = 3;
      descriptor.height = 1;
      const auto curves = compile_curve_stage_luts(state);
      ColorAdjustmentTelemetry telemetry;
      const auto surface = curve_metal.publish_masks_and_color_lut_surface(
          hdr_source, 3, 1, state, {}, descriptor, &telemetry, {}, 1.0, false,
          nullptr, curves.ready() ? &curves : nullptr);
      std::vector<ColorPixel> actual;
      if (!surface || !surface->copy_pixels(&actual) || actual.size() != hdr_source.size()) {
        std::cerr << "HDR curve Metal publication failed: " << telemetry.gpu_fallback_reason << '\n';
        return EXIT_FAILURE;
      }
      for (std::size_t i = 0; i < actual.size(); ++i) {
        if (std::max({std::abs(actual[i].red - hdr_source[i].red),
                      std::abs(actual[i].green - hdr_source[i].green),
                      std::abs(actual[i].blue - hdr_source[i].blue)}) > 2e-5F ||
            actual[i].alpha != hdr_source[i].alpha) {
          std::cerr << "HDR identity curve clipped in Metal variant " << variant << '\n';
          return EXIT_FAILURE;
        }
      }
    }
  }
  auto canonical_working_state =
      photo_editor::pipeline::ColorAdjustmentState{};
  std::vector<std::pair<std::string, photo_editor::pipeline::ColorAdjustmentState>>
      retained_stage_cases;
  canonical_working_state.color_pipeline =
      photo_editor::pipeline::default_color_pipeline_config();
  canonical_working_state.input_transform.exposure = 0.75;
  canonical_working_state.input_transform.temperature = 12.0;
  canonical_working_state.input_transform.tint = -8.0;
  canonical_working_state.contrast = 24.0;
  canonical_working_state.highlights = -28.0;
  canonical_working_state.shadows = 31.0;
  canonical_working_state.whites = 12.0;
  canonical_working_state.blacks = -9.0;
  canonical_working_state.hdr_local_tone_mapping = 64.0;
  canonical_working_state.curve_shadows = 17.0;
  canonical_working_state.curve_lights = -12.0;
  canonical_working_state.master_curve_points = {
      {0.0, 0.0}, {0.35, 0.29}, {0.72, 0.79}, {1.0, 1.0}};
  canonical_working_state.luma_curve_points = {
      {0.0, 0.0}, {0.42, 0.48}, {1.0, 1.0}};
  canonical_working_state.red_curve_points = {
      {0.0, 0.0}, {0.55, 0.61}, {1.0, 1.0}};
  canonical_working_state.saturation = 21.0;
  canonical_working_state.vibrance = 33.0;
  canonical_working_state.rgb_channel_mixer.red_from_red = 94.0;
  canonical_working_state.rgb_channel_mixer.red_from_green = 8.0;
  canonical_working_state.rgb_channel_mixer.red_from_blue = -2.0;
  canonical_working_state.grading_shadows_hue = 0.92;
  canonical_working_state.grading_shadows_saturation = 19.0;
  canonical_working_state.grading_shadows_luminance = -7.0;
  canonical_working_state.grading_midtones_hue = 0.13;
  canonical_working_state.grading_midtones_saturation = -14.0;
  canonical_working_state.grading_midtones_luminance = 5.0;
  canonical_working_state.grading_highlights_hue = 0.56;
  canonical_working_state.grading_highlights_saturation = 23.0;
  canonical_working_state.grading_highlights_luminance = 9.0;
  canonical_working_state.grading_amount = 71.0;
  canonical_working_state.grading_balance = -18.0;
  canonical_working_state.color_mixer_model = "HSL";
  canonical_working_state.color_mixer_channels[0] = {
      .hue = 12.0, .saturation = -17.0, .luminance = 8.0,
      .selective_cyan = 11.0, .selective_magenta = -6.0,
      .selective_yellow = 14.0, .selective_black = -5.0};
  canonical_working_state.color_mixer_channels[4] = {
      .hue = -9.0, .saturation = 13.0, .luminance = -6.0,
      .selective_cyan = -8.0, .selective_magenta = 10.0,
      .selective_yellow = -4.0, .selective_black = 7.0};
  canonical_working_state.color_harmony_enabled = true;
  canonical_working_state.color_harmony_mode = "Triadic";
  canonical_working_state.color_harmony_base_hue = 0.18;
  canonical_working_state.color_harmony_strength = 47.0;
  canonical_working_state.color_replacement_enabled = true;
  canonical_working_state.color_replacement_source_hue = 0.08;
  canonical_working_state.color_replacement_target_hue = 0.61;
  canonical_working_state.color_replacement_range = 76.0;
  canonical_working_state.color_replacement_softness = 63.0;
  canonical_working_state.color_replacement_strength = 69.0;
  canonical_working_state.skin_tone_protection_enabled = true;
  canonical_working_state.skin_tone_line_hue = 0.08;
  canonical_working_state.skin_tone_protection_range = 38.0;
  canonical_working_state.skin_tone_protection_strength = 81.0;
  retained_stage_cases.emplace_back("point edits", canonical_working_state);
  canonical_working_state.lens_distortion = 14.0;
  canonical_working_state.chromatic_aberration = 18.0;
  canonical_working_state.perspective_vertical = 7.0;
  canonical_working_state.perspective_horizontal = -5.0;
  canonical_working_state.transform_rotation = 3.0;
  canonical_working_state.transform_scale = 1.04;
  canonical_working_state.transform_skew_x = 4.0;
  canonical_working_state.transform_skew_y = -3.0;
  canonical_working_state.liquify_strength = 12.0;
  canonical_working_state.liquify_center_x = 0.37;
  canonical_working_state.liquify_center_y = 0.61;
  canonical_working_state.liquify_radius = 0.40;
  canonical_working_state.content_aware_crop_fill = true;
  retained_stage_cases.emplace_back("geometry", canonical_working_state);
  canonical_working_state.texture = 24.0;
  canonical_working_state.clarity = -18.0;
  canonical_working_state.dehaze = 31.0;
  canonical_working_state.sharpening = 42.0;
  canonical_working_state.sharpen_radius = 2.3;
  canonical_working_state.sharpen_detail = 37.0;
  canonical_working_state.sharpen_masking = 54.0;
  canonical_working_state.luminance_nr = 36.0;
  canonical_working_state.color_nr = 41.0;
  retained_stage_cases.emplace_back("detail", canonical_working_state);
  canonical_working_state.photo_layers = {
      photo_editor::pipeline::PhotoLayer{
          .kind = "fill",
          .blend_mode = "normal",
          .opacity = 0.43,
          .fill_color = {0.72F, 0.18F, 0.31F, 0.65F},
          .mask_data = {0U, 255U, 128U, 32U},
          .mask_width = 2,
          .mask_height = 2,
          .mask_inverted = true,
          .mask_opacity = 0.81,
          .mask_density = 0.73,
      },
      photo_editor::pipeline::PhotoLayer{
          .kind = "fill",
          .blend_mode = "screen",
          .opacity = 0.37,
          .clipping_mask = true,
          .fill_color = {0.14F, 0.66F, 0.28F, 0.52F},
      },
      photo_editor::pipeline::PhotoLayer{
          .kind = "fill",
          .blend_mode = "color",
          .opacity = 0.29,
          .fill_color = {0.81F, 0.23F, 0.57F, 0.48F},
      },
      photo_editor::pipeline::PhotoLayer{
          .kind = "adjustment",
          .blend_mode = "normal",
          .opacity = 0.41,
          .clipping_mask = true,
          .exposure = 0.72,
          .contrast = 18.0,
          .highlights = -12.0,
          .shadows = 22.0,
          .whites = 7.0,
          .blacks = -5.0,
          .temperature = 14.0,
          .tint = -9.0,
          .texture = 19.0,
          .clarity = -13.0,
          .dehaze = -27.0,
          .sharpening = 24.0,
          .sharpen_radius = 1.7,
          .sharpen_detail = 33.0,
          .sharpen_masking = 41.0,
          .luminance_nr = 17.0,
          .color_nr = 21.0,
          .lens_distortion = 12.0,
          .lens_vignette = 38.0,
          .chromatic_aberration = 9.0,
          .perspective_vertical = 7.0,
          .perspective_horizontal = -5.0,
          .global_transform_rotation = 3.0,
          .global_transform_scale = 1.04,
          .transform_skew_x = 4.0,
          .transform_skew_y = -3.0,
          .liquify_center_x = 0.38,
          .liquify_center_y = 0.61,
          .liquify_radius = 0.36,
          .liquify_strength = 8.0,
          .content_aware_crop_fill = true,
          .vibrance = 21.0,
          .saturation = -8.0,
          .hue_shift = 13.0,
          .saturation_shift = -17.0,
          .colorize_hue = 0.63,
          .colorize_strength = 28.0,
          .brightness = 6.0,
          .filmic_shoulder = 17.0,
          .filmic_toe = 11.0,
          .adaptive_contrast = 9.0,
          .levels_black_point = 2.0,
          .levels_gamma = 4.0,
          .levels_white_point = -3.0,
          .zone_deep_shadows = 8.0,
          .zone_shadows = -6.0,
          .zone_midtones = 5.0,
          .zone_lights = -4.0,
          .zone_highlights = 7.0,
          .curve_shadows = 4.0,
          .curve_darks = -3.0,
          .curve_lights = 3.0,
          .curve_highlights = -2.0,
          .master_curve_points = {{0.0, 0.0}, {0.50, 0.49}, {1.0, 1.0}},
          .luma_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .red_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .green_curve_points = {{0.0, 0.0}, {0.50, 0.49}, {1.0, 1.0}},
          .blue_curve_points = {{0.0, 0.0}, {0.50, 0.51}, {1.0, 1.0}},
          .color_mixer_model = "HSL",
          .color_mixer_channel = "Blues",
          .hue_adjustment = 8.0,
          .saturation_adjustment = 6.0,
          .luminance_adjustment = -4.0,
          .color_mixer_channels = {{
              {.hue = 7.0, .saturation = -6.0, .luminance = 4.0,
               .selective_cyan = 5.0, .selective_magenta = -3.0,
               .selective_yellow = 4.0, .selective_black = -2.0},
              {}, {}, {},
              {.hue = -5.0, .saturation = 7.0, .luminance = -3.0,
               .selective_cyan = -4.0, .selective_magenta = 5.0,
               .selective_yellow = -3.0, .selective_black = 2.0},
              {}, {}, {},
          }},
          .rgb_channel_mixer = {
              .red_from_red = 96.0, .red_from_green = 6.0, .red_from_blue = -2.0,
              .green_from_red = 2.0, .green_from_green = 96.0, .green_from_blue = 2.0,
              .blue_from_red = -1.0, .blue_from_green = 5.0, .blue_from_blue = 96.0,
          },
          .color_replacement_enabled = true,
          .color_replacement_source_hue = 0.08,
          .color_replacement_target_hue = 0.61,
          .color_replacement_range = 72.0,
          .color_replacement_softness = 58.0,
          .color_replacement_strength = 34.0,
          .skin_tone_protection_enabled = true,
          .skin_tone_line_hue = 0.08,
          .skin_tone_protection_range = 36.0,
          .skin_tone_protection_strength = 61.0,
          .color_harmony_enabled = true,
          .color_harmony_mode = "Triadic",
          .color_harmony_base_hue = 0.17,
          .color_harmony_strength = 31.0,
          .grading_shadows_hue = 0.91,
          .grading_shadows_saturation = 12.0,
          .grading_shadows_luminance = -5.0,
          .grading_midtones_hue = 0.12,
          .grading_midtones_saturation = -9.0,
          .grading_midtones_luminance = 4.0,
          .grading_highlights_hue = 0.57,
          .grading_highlights_saturation = 15.0,
          .grading_highlights_luminance = 6.0,
          .grading_amount = 64.0,
          .grading_balance = -18.0,
      },
      photo_editor::pipeline::PhotoLayer{
          .kind = "adjustment",
          .blend_mode = "normal",
          .opacity = 0.23,
          .black_and_white_enabled = true,
          .black_and_white_mixer = {
              9.0, -6.0, 11.0, -4.0, 5.0, -8.0, 3.0, 7.0},
      },
      photo_editor::pipeline::PhotoLayer{
          .kind = "source",
          .blend_mode = "multiply",
          .opacity = 0.55,
          .pixels = {
              {0.91F, 0.24F, 0.13F, 0.80F},
              {0.18F, 0.82F, 0.31F, 0.62F},
              {0.27F, 0.33F, 0.94F, 0.71F},
              {0.75F, 0.58F, 0.12F, 0.49F},
          },
          .pixel_width = 2,
          .pixel_height = 2,
          .x = 3,
          .y = 4,
          .transform_scale = 1.25,
          .transform_rotation = -13.0,
      },
  };
  auto& retained_layer_lut = canonical_working_state.photo_layers[3];
  retained_layer_lut.lut_enabled = true;
  retained_layer_lut.lut_strength = 62.0;
  retained_layer_lut.lut_size = 2;
  retained_layer_lut.lut_input_color_space = "sRGB";
  retained_layer_lut.lut_output_color_space = "Display P3";
  retained_layer_lut.lut_table = {
      {0.00F, 0.00F, 0.00F, 1.0F}, {0.06F, 0.02F, 0.94F, 1.0F},
      {0.04F, 0.96F, 0.03F, 1.0F}, {0.10F, 0.92F, 0.91F, 1.0F},
      {0.95F, 0.03F, 0.02F, 1.0F}, {0.91F, 0.08F, 0.96F, 1.0F},
      {0.98F, 0.94F, 0.07F, 1.0F}, {1.00F, 1.00F, 1.00F, 1.0F},
  };
  auto srgb_user_lut = photo_editor::pipeline::LutAdjustment{
      .enabled = true,
      .strength = 67.0,
      .name = "srgb-swap",
      .size = 2,
      .input_color_space = "sRGB",
      .output_color_space = "Display P3",
  };
  auto acescct_user_lut = photo_editor::pipeline::LutAdjustment{
      .enabled = true,
      .strength = 43.0,
      .name = "acescct-stack",
      .size = 2,
      .domain_min = {-0.1, -0.1, -0.1},
      .domain_max = {1.1, 1.1, 1.1},
      .input_color_space = "ACEScct",
      .output_color_space = "ACEScg",
  };
  for (auto red = 0; red < 2; ++red) {
    for (auto green = 0; green < 2; ++green) {
      for (auto blue = 0; blue < 2; ++blue) {
        srgb_user_lut.table.push_back({
            .red = static_cast<float>(blue),
            .green = static_cast<float>(red),
            .blue = static_cast<float>(green),
            .alpha = 1.0F,
        });
        acescct_user_lut.table.push_back({
            .red = static_cast<float>(red),
            .green = static_cast<float>(blue),
            .blue = static_cast<float>(green),
            .alpha = 1.0F,
        });
      }
    }
  }
  canonical_working_state.lut_stack = {
      srgb_user_lut, acescct_user_lut};
  auto bounded_working_source =
      std::vector<photo_editor::pipeline::ColorPixel>{};
  bounded_working_source.reserve(source.size());
  for (int y = 0; y < kEdge; ++y) {
    const auto yn = static_cast<float>(y) /
                    static_cast<float>(kEdge - 1);
    for (int x = 0; x < kEdge; ++x) {
      const auto xn = static_cast<float>(x) /
                      static_cast<float>(kEdge - 1);
      bounded_working_source.push_back({
          .red = 0.02F + 0.92F * xn,
          .green = 0.03F + 0.88F * yn,
          .blue = 0.04F + 0.84F * (xn + yn) * 0.5F,
          .alpha = 0.25F + 0.75F * xn,
      });
    }
  }
  retained_stage_cases.emplace_back("layers and LUTs", canonical_working_state);
  for (std::size_t count = 1; count <= canonical_working_state.photo_layers.size(); ++count) {
    auto state = canonical_working_state;
    state.photo_layers.resize(count);
    state.lut_stack.clear();
    retained_stage_cases.emplace_back("layer prefix " + std::to_string(count), std::move(state));
  }
  auto lut_only_state = canonical_working_state;
  lut_only_state.photo_layers.clear();
  retained_stage_cases.emplace_back("global LUT stack", std::move(lut_only_state));
  for (int variant = 0; variant < 3; ++variant) {
    auto state = canonical_working_state;
    if (variant != 1) state.photo_layers[3].dehaze = 0.0;
    if (variant != 2) state.photo_layers[3].lens_vignette = 0.0;
    retained_stage_cases.emplace_back("layer point effects variant " + std::to_string(variant), std::move(state));
  }
  auto stage_metal = photo_editor::pipeline::metal::MetalPipelineBackend{};
  for (const auto* space : {"Linear sRGB", "Linear Display P3", "Linear Rec.2020"}) {
    auto state = canonical_working_state;
    state.color_pipeline.working_profile =
        photo_editor::pipeline::working_color_profile_from_name(space);
    retained_stage_cases.emplace_back(space, std::move(state));
  }
  if (!stage_metal.available()) return EXIT_FAILURE;
  auto stage_parity_ok = true;
  // Two resampling stages and layer compositing accumulate float32 rounding.
  // Bound alpha error to four machine epsilons, independently of RGB parity.
  constexpr auto kRetainedAlphaTolerance = 4.0F * std::numeric_limits<float>::epsilon();
  for (const auto& [label, state] : retained_stage_cases) {
    auto expected = bounded_working_source;
    using namespace photo_editor::pipeline;
    if (!apply_color_scene_adjustments_stage(&expected, kEdge, kEdge, state) ||
        !apply_color_tone_stage(&expected, kEdge, kEdge, state) ||
        !apply_color_curves_stage(&expected, kEdge, kEdge, state) ||
        !apply_color_look_stage(&expected, kEdge, kEdge, state) ||
        !apply_color_layers_stage(&expected, kEdge, kEdge, state, RemovalQuality::Interactive) ||
        !apply_color_detail_stage(&expected, kEdge, kEdge, state, RemovalQuality::Interactive) ||
        !apply_color_geometry_stage(&expected, kEdge, kEdge, state)) {
      std::cerr << "CPU stage failed: " << label << '\n';
      return EXIT_FAILURE;
    }
    const auto lut = photo_editor::desktop::build_canonical_working_stage_lut(state);
    const auto tone = compile_tone_stage_lut(state);
    const auto curves = compile_curve_stage_luts(state);
    const auto look = compile_look_stage_program(state);
    const auto user_luts = compile_user_lut_stage_program(state);
    ColorAdjustmentTelemetry stage_telemetry;
    auto stage_descriptor = working_descriptor;
    stage_descriptor.color_space_identity = state.color_pipeline.working_profile.color_space.name;
    const auto surface = stage_metal.publish_masks_and_color_lut_surface(
        bounded_working_source, kEdge, kEdge, state, lut, stage_descriptor,
        &stage_telemetry, {}, std::exp2(effective_input_exposure(state)), false,
        tone.ready() ? &tone : nullptr, curves.ready() ? &curves : nullptr,
        look.ready() ? &look : nullptr, user_luts.ready() ? &user_luts : nullptr);
    std::vector<ColorPixel> actual;
    if (!surface || !surface->copy_pixels(&actual) || actual.size() != expected.size()) {
      std::cerr << "GPU stage failed: " << label << ' ' << stage_telemetry.gpu_fallback_reason << '\n';
      return EXIT_FAILURE;
    }
    auto error = 0.0F;
    for (std::size_t i = 0; i < actual.size(); ++i) {
      if (!std::isfinite(actual[i].red) || !std::isfinite(actual[i].green) ||
          !std::isfinite(actual[i].blue) || !std::isfinite(actual[i].alpha) ||
          std::abs(actual[i].alpha - expected[i].alpha) > kRetainedAlphaTolerance)
        stage_parity_ok = false;
      error = std::max({error, std::abs(actual[i].red - expected[i].red),
          std::abs(actual[i].green - expected[i].green),
          std::abs(actual[i].blue - expected[i].blue)});
    }
    std::cout << "retained_stage=" << label << " max_error=" << error << '\n';
    stage_parity_ok = stage_parity_ok && error <= 0.004F;
  }
  if (!stage_parity_ok) return EXIT_FAILURE;
  auto exact_working_reference = bounded_working_source;
  auto exact_working_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  if (!photo_editor::pipeline::apply_color_scene_adjustments_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          {}, &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_tone_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          {}, &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_curves_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          {}, &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_look_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          {}, &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_layers_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          photo_editor::pipeline::RemovalQuality::Interactive, {},
          &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_detail_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          photo_editor::pipeline::RemovalQuality::Interactive, {},
          &exact_working_telemetry) ||
      !photo_editor::pipeline::apply_color_geometry_stage(
          &exact_working_reference, kEdge, kEdge, canonical_working_state,
          {}, &exact_working_telemetry)) {
    std::cerr << "canonical unfused working-stage reference failed\n";
    return EXIT_FAILURE;
  }
  const auto canonical_working_lut =
      photo_editor::desktop::build_canonical_working_stage_lut(
          canonical_working_state);
  const auto canonical_tone_lut =
      photo_editor::pipeline::compile_tone_stage_lut(
          canonical_working_state);
  const auto canonical_curve_luts =
      photo_editor::pipeline::compile_curve_stage_luts(
          canonical_working_state);
  const auto canonical_look_program =
      photo_editor::pipeline::compile_look_stage_program(
          canonical_working_state);
  const auto canonical_user_lut_program =
      photo_editor::pipeline::compile_user_lut_stage_program(
          canonical_working_state);
  auto canonical_working_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto canonical_working_surface =
      metal.publish_masks_and_color_lut_surface(
          bounded_working_source,
          kEdge,
          kEdge,
          canonical_working_state,
          canonical_working_lut,
          working_descriptor,
          &canonical_working_telemetry,
          {},
          std::exp2(
              photo_editor::pipeline::effective_input_exposure(
                  canonical_working_state)),
          false,
          &canonical_tone_lut,
          &canonical_curve_luts,
          &canonical_look_program,
          &canonical_user_lut_program);
  auto canonical_working_pixels =
      std::vector<photo_editor::pipeline::ColorPixel>{};
  if (canonical_working_surface == nullptr ||
      !canonical_working_surface->copy_pixels(&canonical_working_pixels) ||
      canonical_working_pixels.size() != exact_working_reference.size() ||
      canonical_working_telemetry.gpu_command_buffer_count != 1 ||
      canonical_working_telemetry.gpu_kernel_dispatch_count != 136 ||
      canonical_working_telemetry.gpu_cpu_to_gpu_bytes !=
          pixel_bytes + static_cast<std::int64_t>(
                            canonical_tone_lut.samples.size() * sizeof(float) +
                            6U * static_cast<std::size_t>(
                                     photo_editor::pipeline::
                                         CompiledCurveStageLuts::kSampleCount) *
                                sizeof(float) +
                            (srgb_user_lut.table.size() +
                             acescct_user_lut.table.size() +
                             retained_layer_lut.lut_table.size()) *
                                sizeof(photo_editor::pipeline::ColorPixel) +
                            4U + 4U * sizeof(photo_editor::pipeline::ColorPixel) +
                            2U * static_cast<std::size_t>(
                                photo_editor::pipeline::CompiledToneStageLut::
                                    kSampleCount) * sizeof(float) +
                            6U * static_cast<std::size_t>(
                                photo_editor::pipeline::CompiledCurveStageLuts::
                                    kSampleCount) * sizeof(float) +
                            6U * 127U * sizeof(float)) ||
      canonical_working_telemetry.gpu_gpu_to_cpu_bytes != 0) {
    std::cerr << "canonical retained working-stage Metal fusion failed: surface="
              << (canonical_working_surface != nullptr)
              << " pixels=" << canonical_working_pixels.size()
              << " commands=" << canonical_working_telemetry.gpu_command_buffer_count
              << " kernels=" << canonical_working_telemetry.gpu_kernel_dispatch_count
              << " upload=" << canonical_working_telemetry.gpu_cpu_to_gpu_bytes
              << " readback=" << canonical_working_telemetry.gpu_gpu_to_cpu_bytes
              << '\n';
    return EXIT_FAILURE;
  }
  const auto tone_lut_cache = metal.tone_lut_cache_stats();
  if (tone_lut_cache.entries != 1U ||
      tone_lut_cache.bytes !=
          canonical_tone_lut.samples.size() * sizeof(float) ||
      tone_lut_cache.entries > tone_lut_cache.maximum_entries ||
      tone_lut_cache.bytes > tone_lut_cache.maximum_bytes) {
    std::cerr << "retained tone LUT cache must expose and honor its bounds\n";
    return EXIT_FAILURE;
  }
  const auto curve_lut_cache = metal.curve_lut_cache_stats();
  if (curve_lut_cache.entries != 1U ||
      curve_lut_cache.bytes !=
          6U * static_cast<std::size_t>(
                   photo_editor::pipeline::CompiledCurveStageLuts::kSampleCount) *
              sizeof(float) ||
      curve_lut_cache.entries > curve_lut_cache.maximum_entries ||
      curve_lut_cache.bytes > curve_lut_cache.maximum_bytes) {
    std::cerr << "retained curve LUT cache must expose and honor its bounds\n";
    return EXIT_FAILURE;
  }
  const auto retained_upload_bytes =
      canonical_working_telemetry.gpu_cpu_to_gpu_bytes;
  const auto reused_working_surface =
      metal.publish_masks_and_color_lut_surface(
          bounded_working_source,
          kEdge,
          kEdge,
          canonical_working_state,
          canonical_working_lut,
          working_descriptor,
          &canonical_working_telemetry,
          {},
          std::exp2(
              photo_editor::pipeline::effective_input_exposure(
                  canonical_working_state)),
          false,
          &canonical_tone_lut,
          &canonical_curve_luts,
          &canonical_look_program,
          &canonical_user_lut_program);
  auto reused_working_pixels =
      std::vector<photo_editor::pipeline::ColorPixel>{};
  if (reused_working_surface == nullptr ||
      !reused_working_surface->copy_pixels(&reused_working_pixels) ||
      reused_working_pixels.size() != canonical_working_pixels.size() ||
      !std::equal(
          reused_working_pixels.cbegin(), reused_working_pixels.cend(),
          canonical_working_pixels.cbegin(),
          [](const auto& left, const auto& right) {
            return left.red == right.red && left.green == right.green &&
                   left.blue == right.blue && left.alpha == right.alpha;
          }) ||
      canonical_working_telemetry.gpu_command_buffer_count != 2 ||
      canonical_working_telemetry.gpu_kernel_dispatch_count != 272 ||
      canonical_working_telemetry.gpu_cpu_to_gpu_bytes -
              retained_upload_bytes != pixel_bytes ||
      canonical_working_telemetry.gpu_gpu_to_cpu_bytes != 0) {
    std::cerr <<
        "reused retained user LUT stack must avoid immutable payload uploads\n";
    return EXIT_FAILURE;
  }
  const auto user_lut_cache = metal.color_lut_cache_stats();
  const auto expected_color_lut_cache_bytes =
      (fused_lut.table.size() + srgb_user_lut.table.size() +
       acescct_user_lut.table.size() + retained_layer_lut.lut_table.size()) *
      sizeof(photo_editor::pipeline::ColorPixel);
  if (user_lut_cache.entries != 4U ||
      user_lut_cache.bytes != expected_color_lut_cache_bytes ||
      user_lut_cache.entries > user_lut_cache.maximum_entries ||
      user_lut_cache.bytes > user_lut_cache.maximum_bytes) {
    std::cerr << "retained user LUT cache must expose and honor its bounds\n";
    return EXIT_FAILURE;
  }
  for (auto index = std::size_t{0}; index < canonical_working_pixels.size();
       ++index) {
    const auto& gpu_pixel = canonical_working_pixels[index];
    const auto& exact_pixel = exact_working_reference[index];
    const auto maximum_channel_error = std::max({
        std::abs(gpu_pixel.red - exact_pixel.red),
        std::abs(gpu_pixel.green - exact_pixel.green),
        std::abs(gpu_pixel.blue - exact_pixel.blue),
    });
    if (maximum_channel_error > 0.004F ||
        std::abs(gpu_pixel.alpha - exact_pixel.alpha) > kRetainedAlphaTolerance) {
      std::cerr << "canonical retained working-stage fusion exceeded unfused parity: pixel="
                << index << " error=" << maximum_channel_error
                << " gpu=" << gpu_pixel.red << ',' << gpu_pixel.green << ',' << gpu_pixel.blue
                << " cpu=" << exact_pixel.red << ',' << exact_pixel.green << ',' << exact_pixel.blue
                << " alpha=" << gpu_pixel.alpha << '/' << exact_pixel.alpha << '\n';
      std::cerr << "alpha_error=" << std::abs(gpu_pixel.alpha - exact_pixel.alpha) << '\n';
      return EXIT_FAILURE;
    }
  }
  auto retained_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto retained_working = metal.publish_masks_and_color_lut_surface(
      source,
      kEdge,
      kEdge,
      fused_state,
      fused_lut,
      working_descriptor,
      &retained_telemetry);
  const auto retained_render_surface = photo_editor::pipeline::RenderSurface{
      .descriptor = working_descriptor,
      .backing = retained_working,
      .identity = "surface:retained-metal-working",
  };
  auto chained_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto chained_output = metal.publish_aces2_output_lut_srgb8_surface(
      retained_working, application, &chained_telemetry, {}, true);
  auto chained_pixels = std::vector<std::uint8_t>(source.size() * 4U);
  if (retained_working == nullptr || !retained_render_surface.valid() ||
      retained_render_surface.byte_size() != static_cast<std::uint64_t>(pixel_bytes) ||
      retained_telemetry.gpu_command_buffer_count != 1 ||
      retained_telemetry.gpu_kernel_dispatch_count != 2 ||
      retained_telemetry.gpu_cpu_to_gpu_bytes != pixel_bytes ||
      retained_telemetry.gpu_gpu_to_cpu_bytes != 0 || chained_output == nullptr ||
      !chained_output->copy_bgra8_pixels_for_testing(chained_pixels) ||
      chained_telemetry.gpu_command_buffer_count != 1 ||
      chained_telemetry.gpu_kernel_dispatch_count != 1 ||
      chained_telemetry.gpu_cpu_to_gpu_bytes >= pixel_bytes ||
      chained_telemetry.gpu_gpu_to_cpu_bytes != 0) {
    std::cerr << "retained Metal working output must chain without pixel readback or re-upload\n";
    return EXIT_FAILURE;
  }
  constexpr auto kSubrectX = 2;
  constexpr auto kSubrectY = 1;
  constexpr auto kSubrectWidth = 3;
  constexpr auto kSubrectHeight = 4;
  auto subrect_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto subrect_output = metal.publish_aces2_output_lut_srgb8_surface(
      retained_working, kSubrectX, kSubrectY, kSubrectWidth, kSubrectHeight,
      application, &subrect_telemetry, {}, true);
  auto subrect_pixels = std::vector<std::uint8_t>(
      static_cast<std::size_t>(kSubrectWidth * kSubrectHeight) * 4U);
  if (subrect_output == nullptr ||
      subrect_output->width() != kSubrectWidth ||
      subrect_output->height() != kSubrectHeight ||
      !subrect_output->copy_bgra8_pixels_for_testing(subrect_pixels) ||
      subrect_telemetry.gpu_command_buffer_count != 1 ||
      subrect_telemetry.gpu_kernel_dispatch_count != 1 ||
      subrect_telemetry.gpu_cpu_to_gpu_bytes != 0 ||
      subrect_telemetry.gpu_gpu_to_cpu_bytes != 0 ||
      metal.publish_aces2_output_lut_srgb8_surface(
          retained_working, kEdge - 1, kEdge - 1, 2, 2,
          application) != nullptr) {
    std::cerr << "retained Metal subrect publication contract failed\n";
    return EXIT_FAILURE;
  }
  for (auto y = 0; y < kSubrectHeight; ++y) {
    for (auto x = 0; x < kSubrectWidth; ++x) {
      const auto source_offset = static_cast<std::size_t>(
          ((kSubrectY + y) * kEdge + kSubrectX + x) * 4);
      const auto target_offset = static_cast<std::size_t>(
          (y * kSubrectWidth + x) * 4);
      if (!std::equal(
              subrect_pixels.cbegin() +
                  static_cast<std::ptrdiff_t>(target_offset),
              subrect_pixels.cbegin() +
                  static_cast<std::ptrdiff_t>(target_offset + 4U),
              chained_pixels.cbegin() +
                  static_cast<std::ptrdiff_t>(source_offset))) {
        std::cerr << "retained Metal subrect differs from full publication\n";
        return EXIT_FAILURE;
      }
    }
  }
  const auto publish_descriptor = photo_editor::pipeline::RenderSurfaceDescriptor{
      .pixel_encoding = photo_editor::pipeline::RenderPixelEncoding::DisplayEncodedFloat,
      .color_domain = photo_editor::pipeline::RenderColorDomain::Device,
      .range = photo_editor::pipeline::RenderRangeModel::DisplayBounded,
      .alpha = photo_editor::pipeline::RenderAlphaModel::Straight,
      .color_space_identity = "sRGB",
      .transfer_identity = "srgb",
      .width = static_cast<std::uint32_t>(kEdge),
      .height = static_cast<std::uint32_t>(kEdge),
  };
  auto chained_render_output = photo_editor::pipeline::RenderSurface{
      .descriptor = publish_descriptor,
      .backing = chained_output,
      .identity = "surface:canonical-native-publish",
  };
  const auto native_output_bytes =
      static_cast<std::uint64_t>(source.size() * 4U);
  if (!chained_render_output.valid() ||
      chained_render_output.byte_size() != native_output_bytes ||
      !chained_render_output.ensure_cpu_pixels() ||
      chained_render_output.pixels.size() != source.size() ||
      chained_render_output.byte_size() !=
          native_output_bytes + source.size() * sizeof(photo_editor::pipeline::ColorPixel)) {
    std::cerr << "canonical Metal output must satisfy the generic retained RenderSurface contract\n";
    return EXIT_FAILURE;
  }
  constexpr auto kCodeScale = 1.0F / 255.0F;
  for (auto index = std::size_t{0}; index < chained_render_output.pixels.size();
       ++index) {
    const auto offset = index * 4U;
    const auto& materialized = chained_render_output.pixels[index];
    if (std::abs(materialized.red -
                 static_cast<float>(chained_pixels[offset + 2U]) * kCodeScale) >
            1.0e-7F ||
        std::abs(materialized.green -
                 static_cast<float>(chained_pixels[offset + 1U]) * kCodeScale) >
            1.0e-7F ||
        std::abs(materialized.blue -
                 static_cast<float>(chained_pixels[offset]) * kCodeScale) >
            1.0e-7F ||
        std::abs(materialized.alpha -
                 static_cast<float>(chained_pixels[offset + 3U]) * kCodeScale) >
            1.0e-7F) {
      std::cerr << "generic Metal output readback must preserve encoded BGRA8 values\n";
      return EXIT_FAILURE;
    }
  }
  auto fused_cpu_output = fused_source;
  photo_editor::pipeline::apply_aces2_output_lut_target_application(
      fused_cpu_output, application);
  for (std::size_t index = 0U; index < fused_cpu_output.size(); ++index) {
    const auto offset = index * 4U;
    const auto expected = std::array<std::uint8_t, 4>{
        srgb8(fused_cpu_output[index].blue),
        srgb8(fused_cpu_output[index].green),
        srgb8(fused_cpu_output[index].red),
        unit8(fused_cpu_output[index].alpha),
    };
    for (std::size_t channel = 0U; channel < expected.size(); ++channel) {
      if (std::abs(static_cast<int>(chained_pixels[offset + channel]) -
                   static_cast<int>(expected[channel])) > 1) {
        std::cerr << "retained Metal working chain exceeded one-code-value CPU parity\n";
        return EXIT_FAILURE;
      }
    }
  }
  auto device_output_config = config;
  device_output_config.preview_output_profile.color_space =
      photo_editor::pipeline::make_linear_rec2020_color_space();
  device_output_config.preview_output_profile.profile_name =
      "Linear Rec.2020 ICC interchange";
  device_output_config.preview_display_target.color_space =
      device_output_config.preview_output_profile.color_space;
  const auto device_application =
      photo_editor::pipeline::compile_aces2_output_lut_application(
          device_output_config,
          photo_editor::pipeline::GamutWarningTarget::PreviewDisplay,
          photo_editor::pipeline::Aces2OutputLutQuality::Final);
  const auto srgb_profile =
      photo_editor::pipeline::builtin_rgb_icc_profile_payload(
          photo_editor::pipeline::make_srgb_color_space());
  const auto device_transform =
      photo_editor::pipeline::compile_builtin_to_icc_transform(
          photo_editor::pipeline::make_linear_rec2020_color_space(),
          srgb_profile,
          photo_editor::pipeline::IccTransformOptions{
              .rendering_intent =
                  photo_editor::pipeline::IccRenderingIntent::Perceptual,
              .black_point_compensation = true,
              .pixel_format = photo_editor::pipeline::IccPixelFormat::RgbaFloat32,
              .target = "metal-retained-device-icc-parity",
          });
  const auto device_lut = photo_editor::pipeline::compile_icc_transform_lut(
      device_transform,
      photo_editor::pipeline::icc_transform_lut_settings(
          photo_editor::pipeline::IccTransformLutQuality::Final));
  auto device_telemetry = photo_editor::pipeline::ColorAdjustmentTelemetry{};
  const auto device_surface =
      device_lut != nullptr
          ? metal.publish_aces2_output_lut_icc_bgra8_surface(
                retained_working,
                device_application,
                *device_lut,
                device_transform,
                publish_descriptor,
                &device_telemetry,
                {},
                true)
          : std::shared_ptr<const
                photo_editor::pipeline::metal::MetalOutputSurface>{};
  auto device_surface_pixels = std::vector<std::uint8_t>(source.size() * 4U);
  if (!device_application.ready() || !device_transform.ready() ||
      device_lut == nullptr || !device_lut->valid() ||
      device_lut->unsafe_cell_count == 0U || device_surface == nullptr ||
      !device_surface->valid_for(publish_descriptor) ||
      !device_surface->copy_bgra8_pixels_for_testing(device_surface_pixels) ||
      device_telemetry.gpu_command_buffer_count < 1 ||
      device_telemetry.gpu_kernel_dispatch_count < 3 ||
      device_telemetry.gpu_blit_count != 0 ||
      device_telemetry.gpu_gpu_to_cpu_bytes <
          static_cast<std::int64_t>(sizeof(std::uint32_t)) ||
      device_telemetry.gpu_gpu_to_cpu_bytes >= pixel_bytes) {
    std::cerr << "retained ACES-to-device ICC publication must use bounded exact correction\n";
    return EXIT_FAILURE;
  }
  auto exact_device_pixels = fused_source;
  photo_editor::pipeline::apply_aces2_output_lut_target_application(
      exact_device_pixels, device_application);
  auto exact_icc_pixels = std::vector<photo_editor::pipeline::IccRgbaFloat>{};
  exact_icc_pixels.reserve(exact_device_pixels.size());
  for (const auto& pixel : exact_device_pixels) {
    exact_icc_pixels.push_back({
        .red = pixel.red,
        .green = pixel.green,
        .blue = pixel.blue,
        .alpha = pixel.alpha,
    });
  }
  if (!device_transform.apply(exact_icc_pixels)) {
    std::cerr << "exact device ICC reference transform failed\n";
    return EXIT_FAILURE;
  }
  for (std::size_t index = 0U; index < exact_icc_pixels.size(); ++index) {
    const auto offset = index * 4U;
    const auto& exact = exact_icc_pixels[index];
    const auto expected = std::array<std::uint8_t, 4>{
        unit8(exact.blue),
        unit8(exact.green),
        unit8(exact.red),
        unit8(exact.alpha),
    };
    for (std::size_t channel = 0U; channel < expected.size(); ++channel) {
      if (std::abs(static_cast<int>(device_surface_pixels[offset + channel]) -
                   static_cast<int>(expected[channel])) > 1) {
        std::cerr << "retained device ICC publication exceeded one-code-value parity\n";
        return EXIT_FAILURE;
      }
    }
  }
  auto invalid_fused_lut = fused_lut;
  invalid_fused_lut.table.pop_back();
  auto invalid_fused_pixels = source;
  auto invalid_fused_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  if (metal.apply_masks_and_color_lut(
          &invalid_fused_pixels,
          kEdge,
          kEdge,
          fused_state,
          invalid_fused_lut,
          &invalid_fused_telemetry) ||
      invalid_fused_telemetry.gpu_fallback_reason !=
          "fused_masks_color_lut_shape_invalid" ||
      invalid_fused_pixels.size() != source.size()) {
    std::cerr
        << "the fused Metal path must fail closed on a malformed LUT shape\n";
    return EXIT_FAILURE;
  }
  const auto cancellation_requested =
      std::atomic_bool{true};
  auto cancelled_fused_pixels = source;
  auto cancelled_fused_telemetry =
      photo_editor::pipeline::ColorAdjustmentTelemetry{};
  if (metal.apply_masks_and_color_lut(
          &cancelled_fused_pixels,
          kEdge,
          kEdge,
          fused_state,
          fused_lut,
          &cancelled_fused_telemetry,
          photo_editor::pipeline::
              CancellationToken{
                  .cancellation_requested =
                      &cancellation_requested,
              }) ||
      cancelled_fused_telemetry
              .gpu_fallback_reason !=
          "fused_masks_color_lut_cancelled") {
    std::cerr
        << "the fused Metal path must reject a pre-canceled dispatch\n";
    return EXIT_FAILURE;
  }
  for (int identity_index = 0;
       identity_index < 5;
       ++identity_index) {
    auto distinct_state = fused_state;
    distinct_state.saturation =
        20.0 +
        static_cast<double>(identity_index);
    const auto distinct_lut =
        photo_editor::pipeline::build_color_lut(
            distinct_state);
    auto distinct_pixels = source;
    if (!metal.apply_masks_and_color_lut(
            &distinct_pixels,
            kEdge,
            kEdge,
            distinct_state,
            distinct_lut,
            &fused_telemetry)) {
      std::cerr
          << "distinct fused Metal LUT dispatch failed during cache stress\n";
      return EXIT_FAILURE;
    }
    const auto stressed_cache =
        metal.color_lut_cache_stats();
    if (stressed_cache.entries >
            stressed_cache.maximum_entries ||
        stressed_cache.bytes >
            stressed_cache.maximum_bytes) {
      std::cerr
          << "the fused Metal LUT cache exceeded its aggregate ceiling under churn\n";
      return EXIT_FAILURE;
    }
  }
  const auto upload_before_evicted_reuse =
      fused_telemetry.gpu_cpu_to_gpu_bytes;
  fused_source = source;
  if (!metal.apply_masks_and_color_lut(
          &fused_source,
          kEdge,
          kEdge,
          fused_state,
          fused_lut,
          &fused_telemetry) ||
      fused_telemetry.gpu_cpu_to_gpu_bytes -
              upload_before_evicted_reuse !=
          pixel_bytes +
              static_cast<std::int64_t>(
                  fused_lut.table.size() *
                  sizeof(
                      photo_editor::pipeline::
                          ColorPixel))) {
    std::cerr
        << "an evicted fused Metal LUT must be uploaded exactly once when reused\n";
    return EXIT_FAILURE;
  }
  const auto first_upload_bytes = telemetry.gpu_cpu_to_gpu_bytes;
  gpu = source;
  if (!metal.apply_aces2_output_lut_target(
          &gpu,
          kEdge,
          kEdge,
          application,
          &telemetry) ||
      telemetry.gpu_command_buffer_count != 2 ||
      telemetry.gpu_kernel_dispatch_count != 2 ||
      telemetry.gpu_cpu_to_gpu_bytes - first_upload_bytes != pixel_bytes ||
      telemetry.gpu_gpu_to_cpu_bytes != pixel_bytes * 2) {
    std::cerr << "reused Metal ACES 2 output LUTs must not upload the cube again\n";
    return EXIT_FAILURE;
  }
  auto interactive_cpu = source;
  for (std::size_t index = 0U; index < interactive_cpu.size(); ++index) {
    interactive_cpu[index] =
        photo_editor::pipeline::apply_aces2_output_lut_target_application(
            interactive_application, source[index]);
  }
  ParityMetrics interactive_metrics;
  const auto before_interactive_upload = telemetry.gpu_cpu_to_gpu_bytes;
  gpu = source;
  if (!metal.apply_aces2_output_lut_target(
          &gpu,
          kEdge,
          kEdge,
          interactive_application,
          &telemetry) ||
      telemetry.gpu_cpu_to_gpu_bytes - before_interactive_upload !=
          pixel_bytes + static_cast<std::int64_t>(
                            interactive_application.lut->byte_size())) {
    std::cerr << "the interactive Metal ACES 2 tier must upload exactly one second cube\n";
    return EXIT_FAILURE;
  }
  if (!measure_parity(
          interactive_cpu,
          gpu,
          config.preview_display_target.color_space,
          &interactive_metrics) ||
      !parity_passes(interactive_metrics)) {
    std::cerr << "Metal ACES 2 65^3 output LUT exceeds the CPU/GPU parity gate\n";
    return EXIT_FAILURE;
  }
  const auto before_final_reuse = telemetry.gpu_cpu_to_gpu_bytes;
  gpu = source;
  if (!metal.apply_aces2_output_lut_target(
          &gpu,
          kEdge,
          kEdge,
          application,
          &telemetry) ||
      telemetry.gpu_cpu_to_gpu_bytes - before_final_reuse != pixel_bytes ||
      telemetry.gpu_command_buffer_count != 4 ||
      telemetry.gpu_kernel_dispatch_count != 4 ||
      telemetry.gpu_gpu_to_cpu_bytes != pixel_bytes * 4) {
    std::cerr << "alternating Metal ACES 2 tiers must reuse both cached cube buffers\n";
    return EXIT_FAILURE;
  }

  std::cout << "Metal ACES 2 output LUT parity: final_97_p99_delta_e_00="
            << final_metrics.p99_delta_e_00
            << " final_97_max_delta_e_00=" << final_metrics.maximum_delta_e_00
            << " interactive_65_p99_delta_e_00="
            << interactive_metrics.p99_delta_e_00
            << " interactive_65_max_delta_e_00="
            << interactive_metrics.maximum_delta_e_00 << '\n';
  auto tagged_manifest_sha256 = QByteArray{};
  auto tagged_artifact_set_sha256 = QString{};
  auto tagged_set_id = QString{};
  auto tagged_reference_count = 0;
  auto real_pixel_count = std::size_t{0U};
  auto p99_gate_exceedances = std::size_t{0U};
  auto p99_allowed_exceedances = std::size_t{0U};
  auto real_maximum = 0.0;
  auto real_maximum_channel_error = 0.0;
  std::vector<double> tagged_dispatch_wall_ms;
  constexpr auto kTaggedDispatchP95GateMs = 25.0;
  constexpr auto kTaggedDispatchMaximumGateMs = 50.0;
  if (!tagged_set_path.isEmpty()) {
    QFile manifest_file(tagged_set_path);
    if (!manifest_file.open(QIODevice::ReadOnly)) {
      std::cerr << "unable to read tagged-reference set manifest\n";
      return EXIT_FAILURE;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(
        manifest_file.readAll(), &parse_error);
    const auto manifest = document.object();
    const auto references = manifest.value(QStringLiteral("references")).toArray();
    const auto tagger = manifest.value(QStringLiteral("tagger")).toObject();
    const auto artifact_summary =
        manifest.value(QStringLiteral("artifact_summary")).toObject();
    const auto expected_icc_sha256 =
        tagger.value(QStringLiteral("icc_profile_sha256")).toString().toLatin1();
    tagged_set_id = manifest.value(QStringLiteral("set_id")).toString();
    tagged_artifact_set_sha256 =
        artifact_summary.value(QStringLiteral("artifact_set_sha256")).toString();
    tagged_manifest_sha256 = sha256_file(tagged_set_path);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject() ||
        manifest.value(QStringLiteral("schema")).toString() !=
            QStringLiteral("photo-editor-independent-chart-tagged-set:v1") ||
        manifest.value(QStringLiteral("promotion_eligible")).toBool(true) ||
        references.size() != 48 || expected_icc_sha256.size() != 64 ||
        tagged_set_id.isEmpty() || tagged_manifest_sha256.size() != 64 ||
        tagged_artifact_set_sha256.size() != 64) {
      std::cerr << "invalid tagged-reference set contract\n";
      return EXIT_FAILURE;
    }
    tagged_reference_count = static_cast<int>(references.size());
    const auto linear_srgb_to_working =
        photo_editor::pipeline::compile_builtin_color_transform(
            photo_editor::pipeline::make_linear_srgb_color_space(),
            config.working_profile.color_space)
            .matrix;
    const auto artifact_root = QFileInfo(tagged_set_path).absoluteDir();
    std::vector<photo_editor::pipeline::ColorPixel> chart_source;
    for (const auto& reference_value : references) {
      if (!reference_value.isObject()) {
        std::cerr << "tagged-reference entry must be an object\n";
        return EXIT_FAILURE;
      }
      std::string failure_reason;
      if (!load_tagged_reference_pixels(
              reference_value.toObject(), artifact_root, expected_icc_sha256,
              linear_srgb_to_working, &chart_source, &failure_reason)) {
        std::cerr << failure_reason << '\n';
        return EXIT_FAILURE;
      }
      auto chart_cpu = chart_source;
      for (std::size_t index = 0U; index < chart_cpu.size(); ++index) {
        chart_cpu[index] =
            photo_editor::pipeline::apply_aces2_output_lut_target_application(
                application, chart_source[index]);
      }
      auto chart_gpu = chart_source;
      const auto dispatch_start = std::chrono::steady_clock::now();
      if (!metal.apply_aces2_output_lut_target(
              &chart_gpu, 606, 404, application, &telemetry)) {
        std::cerr << "Metal dispatch failed for a tagged chart reference\n";
        return EXIT_FAILURE;
      }
      tagged_dispatch_wall_ms.push_back(
          std::chrono::duration<double, std::milli>(
              std::chrono::steady_clock::now() - dispatch_start)
              .count());
      for (std::size_t index = 0U; index < chart_cpu.size(); ++index) {
        const auto& expected = chart_cpu[index];
        const auto& actual = chart_gpu[index];
        if (!std::isfinite(actual.red) || !std::isfinite(actual.green) ||
            !std::isfinite(actual.blue) || !std::isfinite(actual.alpha) ||
            std::abs(actual.alpha - expected.alpha) > 1.0e-6F) {
          std::cerr << "real chart Metal result is non-finite or changed alpha\n";
          return EXIT_FAILURE;
        }
        const auto delta = photo_editor::pipeline::perceptual_delta(
            expected, actual, config.preview_display_target.color_space);
        if (delta > 0.25) {
          ++p99_gate_exceedances;
        }
        real_maximum = std::max(real_maximum, delta);
        real_maximum_channel_error = std::max({
            real_maximum_channel_error,
            std::abs(static_cast<double>(actual.red - expected.red)),
            std::abs(static_cast<double>(actual.green - expected.green)),
            std::abs(static_cast<double>(actual.blue - expected.blue)),
        });
      }
      real_pixel_count += chart_cpu.size();
    }
    p99_allowed_exceedances =
        real_pixel_count - static_cast<std::size_t>(
                               std::ceil(0.99 *
                                         static_cast<double>(real_pixel_count)));
    std::sort(tagged_dispatch_wall_ms.begin(), tagged_dispatch_wall_ms.end());
    const auto dispatch_p95_ms = percentile(tagged_dispatch_wall_ms, 0.95);
    const auto dispatch_maximum_ms = tagged_dispatch_wall_ms.back();
    std::cout << "Metal ACES 2 tagged chart parity: references="
              << references.size() << " pixels=" << real_pixel_count
              << " delta_e_00_over_0.25=" << p99_gate_exceedances
              << " allowed=" << p99_allowed_exceedances
              << " max_delta_e_00=" << real_maximum
              << " max_channel_error=" << real_maximum_channel_error
              << " dispatch_p95_ms=" << dispatch_p95_ms
              << " dispatch_max_ms=" << dispatch_maximum_ms << '\n';
    if (p99_gate_exceedances > p99_allowed_exceedances ||
        real_maximum > 0.75 || real_maximum_channel_error > 2.0e-4) {
      std::cerr << "Metal ACES 2 tagged chart parity exceeds the release gate\n";
      return EXIT_FAILURE;
    }
    if (dispatch_p95_ms > kTaggedDispatchP95GateMs ||
        dispatch_maximum_ms > kTaggedDispatchMaximumGateMs) {
      std::cerr << "Metal ACES 2 tagged chart dispatch exceeds the release latency gate\n";
      return EXIT_FAILURE;
    }
  }
  if (!arguments.report_path.isEmpty()) {
    const auto device_name = metal.device_name();
    if (device_name.empty()) {
      std::cerr << "Metal device proof requires a named physical device\n";
      return EXIT_FAILURE;
    }
    const auto lut_cache_key_sha256 = QCryptographicHash::hash(
        QByteArray::fromStdString(application.lut->cache_key),
        QCryptographicHash::Sha256).toHex();
    const auto interactive_lut_cache_key_sha256 = QCryptographicHash::hash(
        QByteArray::fromStdString(interactive_application.lut->cache_key),
        QCryptographicHash::Sha256).toHex();
    const auto report = QJsonObject{
        {QStringLiteral("schema"),
         QStringLiteral("photo-editor-metal-aces2-device-proof:v2")},
        {QStringLiteral("tool_version"),
         QStringLiteral("photo-editor-metal-aces2-output-parity:v2")},
        {QStringLiteral("hardware"),
         QJsonObject{
             {QStringLiteral("backend"), QStringLiteral("Metal")},
             {QStringLiteral("device_name"), QString::fromStdString(device_name)},
             {QStringLiteral("kernel_type"), QSysInfo::kernelType()},
             {QStringLiteral("kernel_version"), QSysInfo::kernelVersion()},
             {QStringLiteral("product_type"), QSysInfo::productType()},
             {QStringLiteral("product_version"), QSysInfo::productVersion()},
             {QStringLiteral("cpu_architecture"), QSysInfo::currentCpuArchitecture()},
             {QStringLiteral("qt_version"), QString::fromLatin1(qVersion())},
             {QStringLiteral("build_mode"), QString::fromLatin1(kBuildMode)},
         }},
        {QStringLiteral("luts"),
         QJsonObject{
             {QStringLiteral("final_cube_size"), application.lut->settings.cube_size},
             {QStringLiteral("interactive_cube_size"),
              interactive_application.lut->settings.cube_size},
             {QStringLiteral("final_cache_key_sha256"),
              QString::fromLatin1(lut_cache_key_sha256)},
             {QStringLiteral("interactive_cache_key_sha256"),
              QString::fromLatin1(interactive_lut_cache_key_sha256)},
         }},
        {QStringLiteral("synthetic_parity"),
         QJsonObject{
             {QStringLiteral("final_97"), parity_contract(final_metrics)},
             {QStringLiteral("interactive_65"),
              parity_contract(interactive_metrics)},
         }},
        {QStringLiteral("tagged_chart_parity"),
         QJsonObject{
             {QStringLiteral("required"), !tagged_set_path.isEmpty()},
             {QStringLiteral("set_id"), tagged_set_id},
             {QStringLiteral("manifest_sha256"),
              QString::fromLatin1(tagged_manifest_sha256)},
             {QStringLiteral("artifact_set_sha256"),
              tagged_artifact_set_sha256},
             {QStringLiteral("reference_count"), tagged_reference_count},
             {QStringLiteral("pixel_count"),
              static_cast<qint64>(real_pixel_count)},
             {QStringLiteral("delta_e_00_over_0_25"),
              static_cast<qint64>(p99_gate_exceedances)},
             {QStringLiteral("allowed_exceedances"),
              static_cast<qint64>(p99_allowed_exceedances)},
             {QStringLiteral("maximum_delta_e_00"), real_maximum},
             {QStringLiteral("maximum_channel_error"),
              real_maximum_channel_error},
             {QStringLiteral("maximum_gate"), 0.75},
             {QStringLiteral("channel_gate"), 2.0e-4},
         }},
        {QStringLiteral("timing"),
         QJsonObject{
             {QStringLiteral("sample_count"),
              static_cast<qint64>(tagged_dispatch_wall_ms.size())},
             {QStringLiteral("pixel_count_per_sample"), 606 * 404},
             {QStringLiteral("p95_wall_ms"),
              percentile(tagged_dispatch_wall_ms, 0.95)},
             {QStringLiteral("maximum_wall_ms"),
              tagged_dispatch_wall_ms.empty() ? 0.0
                                              : tagged_dispatch_wall_ms.back()},
             {QStringLiteral("p95_gate_ms"), kTaggedDispatchP95GateMs},
             {QStringLiteral("maximum_gate_ms"),
              kTaggedDispatchMaximumGateMs},
             {QStringLiteral("telemetry_final_output_ms"),
              telemetry.final_output_ms},
         }},
        {QStringLiteral("telemetry"),
         QJsonObject{
             {QStringLiteral("command_buffer_count"),
              static_cast<qint64>(telemetry.gpu_command_buffer_count)},
             {QStringLiteral("kernel_dispatch_count"),
              static_cast<qint64>(telemetry.gpu_kernel_dispatch_count)},
             {QStringLiteral("cpu_to_gpu_bytes"),
              telemetry.gpu_cpu_to_gpu_bytes},
             {QStringLiteral("gpu_to_cpu_bytes"),
              telemetry.gpu_gpu_to_cpu_bytes},
         }},
        {QStringLiteral("result"), QStringLiteral("passed")},
    };
    QSaveFile report_file(arguments.report_path);
    if (!report_file.open(QIODevice::WriteOnly) ||
        report_file.write(
            QJsonDocument(report).toJson(QJsonDocument::Indented)) < 0 ||
        !report_file.commit()) {
      std::cerr << "unable to publish Metal device proof report\n";
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
