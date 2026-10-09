#include "app_bootstrap.h"
#include "canonical_native_preview_suffix.h"
#include "desktop_pipeline_state_compiler.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "edit_recipe_pipeline_bridge.h"
#include "photo_layer_compiler.h"
#include "photo_fragment_brush_preset_store.h"
#include "photo_render_utils.h"
#include "photo_text_style.h"
#include "preview_color_pipeline_config.h"
#include "preview_device_icc_options.h"
#include "render_plan_store.h"
#include "system_monitor_profile_state.h"
#include "working_image.h"
#include "photo_editor/pipeline/metal/metal_pipeline_backend.h"

#include <QByteArray>
#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QTemporaryDir>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

using namespace photo_editor::desktop;
using namespace photo_editor::pipeline;

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
SystemMonitorProfileSnapshot refresh_system_monitor_profile_for_test();
}

namespace {

constexpr int kWidth = 192;
constexpr int kHeight = 112;

struct TestFailure final {
  QString message;
};

class TestEditorPreviewImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    images_.insert(key, image);
  }

  void clearImage(const QString& key) override { images_.remove(key); }

  [[nodiscard]] QImage image(const QString& key) const override {
    return images_.value(key);
  }

 private:
  QHash<QString, QImage> images_;
};

QVariantMap plain_text_layer(const QString& id, const QString& text = QStringLiteral("INK")) {
  return {
      {QStringLiteral("id"), id},
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("name"), id},
      {QStringLiteral("text"), text},
      {QStringLiteral("fontFamily"), QStringLiteral("Helvetica")},
      {QStringLiteral("fontSize"), 52},
      {QStringLiteral("textColor"), QStringLiteral("#ffffff")},
      {QStringLiteral("pixelWidth"), kWidth},
      {QStringLiteral("pixelHeight"), kHeight},
      {QStringLiteral("canvasWidth"), kWidth},
      {QStringLiteral("canvasHeight"), kHeight},
      {QStringLiteral("x"), 0},
      {QStringLiteral("y"), 0},
  };
}

QImage sdr_source() {
  QImage image(kWidth, kHeight, QImage::Format_RGBA8888);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(
          28 + (x * 3 + y) % 96,
          42 + (x + y * 2) % 112,
          58 + (x * 2 + y * 3) % 120));
    }
  }
  return image;
}

QImage hdr_source() {
  QImage image(kWidth, kHeight, QImage::Format_RGBA32FPx4);
  image.setColorSpace(QColorSpace(QColorSpace::SRgbLinear));
  for (int y = 0; y < image.height(); ++y) {
    auto* row = reinterpret_cast<QRgbaFloat32*>(image.scanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      row[x] = QRgbaFloat32{0.18F, 0.18F, 0.18F, 1.0F};
    }
  }
  // Keep a scene-referred highlight well outside the centered glyphs. It must
  // retain its exact rendered value when a display-referred overlay is added.
  auto* highlight = reinterpret_cast<QRgbaFloat32*>(image.scanLine(1));
  highlight[1] = QRgbaFloat32{8.0F, 8.0F, 8.0F, 1.0F};
  return image;
}

ColorAdjustmentState compile_state(
    const EditRecipeState& recipe,
    const QSize& size,
    const ColorPipelineConfig& config) {
  return compile_desktop_pipeline_state(
      recipe, nullptr, config, DesktopRenderPurpose::Output,
      LocalMaskRasterPolicy::UseStoredRaster, size);
}

std::optional<RenderPlan> admitted_plan(
    const QString& asset_id,
    const EditRecipeState& recipe,
    const QSize& size,
    DisplayRenderingTarget target) {
  const auto admission = RenderPlanStore::shared().admit(
      asset_id, QStringLiteral("synthetic-display-text-overlay"), recipe, size,
      RenderPurpose::Export, target);
  if (!admission.admitted || !admission.plan.has_value()) {
    std::cerr << "Render plan admission failed: " << admission.error.toStdString() << '\n';
    return std::nullopt;
  }
  return admission.plan;
}

QImage render_with_plan(
    const QImage& source,
    const EditRecipeState& state,
    const RenderPlan& plan) {
  return render_output_image_with_backend(
      source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, nullptr, {},
      RemovalQuality::Final, RenderOutputPolicy::DisplayReferred, &plan);
}

std::vector<PhotoLayer> compile_all_layers(
    const EditRecipeState& recipe,
    const ColorPipelineConfig& config) {
  return compile_pipeline_photo_layers(
      recipe.layers, recipe.mask_stack, LocalMaskRasterPolicy::UseStoredRaster,
      QSize(kWidth, kHeight), {}, config, DesktopRenderPurpose::Output);
}

bool has_layer_id(const std::vector<PhotoLayer>& layers, const QString& id) {
  return std::any_of(layers.cbegin(), layers.cend(), [&id](const PhotoLayer& layer) {
    return QString::fromStdString(layer.id) == id;
  });
}

double srgb_to_linear(double channel) {
  return channel <= 0.04045 ? channel / 12.92
                            : std::pow((channel + 0.055) / 1.055, 2.4);
}

QColor linear_srgb_color(const QColor& encoded) {
  return QColor::fromRgbF(
      static_cast<float>(srgb_to_linear(encoded.redF())),
      static_cast<float>(srgb_to_linear(encoded.greenF())),
      static_cast<float>(srgb_to_linear(encoded.blueF())),
      static_cast<float>(encoded.alphaF()));
}

bool same_pixel(const QImage& left, const QImage& right, int x, int y) {
  return left.pixel(x, y) == right.pixel(x, y);
}

bool same_image_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  for (int y = 0; y < left.height(); ++y)
    for (int x = 0; x < left.width(); ++x)
      if (!same_pixel(left, right, x, y)) return false;
  return true;
}

bool verify_overlay_pixels(
    const QImage& baseline,
    const QImage& rendered,
    const PhotoLayer& overlay,
    const QColor& authored_color,
    QString* error,
    const QString& route_context = {}) {
  if (baseline.isNull() || rendered.isNull() || baseline.size() != rendered.size() ||
      overlay.pixel_width != kWidth || overlay.pixel_height != kHeight ||
      overlay.pixels.size() != static_cast<std::size_t>(kWidth * kHeight)) {
    *error = QStringLiteral("The overlay regression did not produce matching full-size SDR renders.");
    return false;
  }
  auto found_opaque_ink = false;
  auto found_edge_ink = false;
  for (int y = 0; y < kHeight; ++y) {
    for (int x = 0; x < kWidth; ++x) {
      const auto index = static_cast<std::size_t>(y * kWidth + x);
      const auto alpha = std::clamp(static_cast<double>(overlay.pixels[index].alpha), 0.0, 1.0);
      if (alpha <= 1.0e-7) {
        if (!same_pixel(baseline, rendered, x, y)) {
          const auto before = baseline.pixelColor(x, y);
          const auto after = rendered.pixelColor(x, y);
          *error = QStringLiteral(
              "A display-referred text overlay changed a pixel outside its ink coverage "
              "at (%1,%2): alpha=%3, baseline RGB=(%4,%5,%6), rendered RGB=(%7,%8,%9). %10")
              .arg(x).arg(y).arg(alpha, 0, 'f', 7)
              .arg(before.red()).arg(before.green()).arg(before.blue())
              .arg(after.red()).arg(after.green()).arg(after.blue())
              .arg(route_context);
          return false;
        }
        continue;
      }
      const auto actual = rendered.pixelColor(x, y);
      if (alpha >= 0.995) {
        found_opaque_ink = true;
        if (std::abs(actual.red() - authored_color.red()) > 2 ||
            std::abs(actual.green() - authored_color.green()) > 2 ||
            std::abs(actual.blue() - authored_color.blue()) > 2) {
          *error = QStringLiteral("Opaque text did not retain its authored display color.");
          return false;
        }
      } else if (!found_edge_ink && alpha >= 0.15 && alpha <= 0.85) {
        const auto background = linear_srgb_color(baseline.pixelColor(x, y));
        const auto authored_linear = linear_srgb_color(authored_color);
        const auto expected = QColor::fromRgbF(
            static_cast<float>(alpha * authored_linear.redF() +
                (1.0 - alpha) * background.redF()),
            static_cast<float>(alpha * authored_linear.greenF() +
                (1.0 - alpha) * background.greenF()),
            static_cast<float>(alpha * authored_linear.blueF() +
                (1.0 - alpha) * background.blueF()));
        const auto actual_linear = linear_srgb_color(actual);
        const auto max_delta = std::max({
            std::abs(actual_linear.redF() - expected.redF()),
            std::abs(actual_linear.greenF() - expected.greenF()),
            std::abs(actual_linear.blueF() - expected.blueF()),
        });
        if (max_delta > 0.025) {
          *error = QStringLiteral("Antialiased text edges were not composited in linear display light.");
          return false;
        }
        found_edge_ink = true;
      }
    }
  }
  if (!found_opaque_ink || !found_edge_ink) {
    *error = QStringLiteral("The text fixture did not contain both opaque and antialiased ink pixels.");
    return false;
  }
  return true;
}

QImage preview_result_pixels_for_test(const RenderResult& result) {
  if (!result.preview_image.isNull()) return result.preview_image;
  if (result.texture_upload_surface == nullptr ||
      result.texture_upload_surface->format() !=
          photo_editor::pipeline::metal::MetalOutputSurfaceFormat::Bgra8UnormSrgbEncoded ||
      result.texture_upload_surface->width() <= 0 ||
      result.texture_upload_surface->height() <= 0) {
    return {};
  }
  const auto width = result.texture_upload_surface->width();
  const auto height = result.texture_upload_surface->height();
  std::vector<std::uint8_t> bgra(static_cast<std::size_t>(width) *
      static_cast<std::size_t>(height) * 4U);
  if (!result.texture_upload_surface->copy_bgra8_pixels_for_testing(bgra)) return {};
  QImage image(width, height, QImage::Format_ARGB32);
  if (image.isNull()) return {};
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto offset = (static_cast<std::size_t>(y) *
          static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4U;
      image.setPixel(x, y, qRgba(bgra[offset + 2U], bgra[offset + 1U],
          bgra[offset], bgra[offset + 3U]));
    }
  }
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  return image;
}

int maximum_channel_delta(const QImage& left, const QImage& right, QPoint* first_delta = nullptr) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return 256;
  auto maximum = 0;
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto a = left.pixelColor(x, y);
      const auto b = right.pixelColor(x, y);
      const auto delta = std::max({
          std::abs(a.red() - b.red()),
          std::abs(a.green() - b.green()),
          std::abs(a.blue() - b.blue()),
          std::abs(a.alpha() - b.alpha()),
      });
      if (delta > maximum) {
        maximum = delta;
        if (first_delta != nullptr) *first_delta = QPoint(x, y);
      }
    }
  }
  return maximum;
}

bool verify_unsupported_fallback(
    const QImage& source,
    EditRecipeState recipe,
    const QString& expected_layer_id,
    const QString& label) {
  const auto config = compile_color_pipeline_config(recipe);
  const auto compiled = compile_state(recipe, source.size(), config);
  const auto all_layers = compile_all_layers(recipe, config);
  if (!compiled.display_referred_text_layers.empty() ||
      !has_layer_id(compiled.photo_layers, expected_layer_id) ||
      !has_layer_id(all_layers, expected_layer_id)) {
    std::cerr << label.toStdString()
              << " must remain in the ordinary working-scene layer stack\n";
    return false;
  }
  const auto plan = admitted_plan(
      QStringLiteral("display-text-fallback-") + label,
      recipe, source.size(), DisplayRenderingTarget::SrgbSimulation);
  if (!plan.has_value()) return false;
  const auto rendered = render_output_image_with_compiled_state(
      source, recipe, compiled, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, *plan);
  auto explicit_legacy_state = compiled;
  explicit_legacy_state.photo_layers = all_layers;
  explicit_legacy_state.display_referred_text_layers.clear();
  const auto legacy_rendered = render_output_image_with_compiled_state(
      source, recipe, explicit_legacy_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, *plan);
  if (rendered.isNull() || legacy_rendered.isNull()) {
    std::cerr << label.toStdString() << " fallback render failed\n";
    return false;
  }
  if (!same_image_pixels(rendered, legacy_rendered)) {
    std::cerr << label.toStdString()
              << " did not preserve the explicit legacy working-scene render exactly\n";
    return false;
  }
  return true;
}

bool verify_native_preview_suffix_parity(
    const QImage& source,
    EditRecipeState baseline_state,
    EditRecipeState overlay_state,
    const PhotoLayer& overlay_layer,
    QString* error) {
  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    *error = QStringLiteral("Could not create the isolated native preview test directory.");
    return false;
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QCoreApplication::setApplicationName(QStringLiteral("DisplayTextOverlayPreviewTest"));
  if (!AppBootstrap::ensureRuntimeDirectories()) {
    *error = QStringLiteral("Could not initialize the isolated native preview runtime.");
    return false;
  }
  const auto source_path = runtime_root.filePath(QStringLiteral("preview-source.png"));
  if (!source.save(source_path)) {
    *error = QStringLiteral("Could not write the native preview source fixture.");
    return false;
  }

  const auto configure_preview_state = [](EditRecipeState state) {
    state.preview_color_space = QStringLiteral("sRGB");
    state.preview_output_mode = QStringLiteral("Reference Display Target");
    return state;
  };
  baseline_state = configure_preview_state(std::move(baseline_state));
  overlay_state = configure_preview_state(std::move(overlay_state));
  const auto make_request = [&](const EditRecipeState& state, bool allow_native_suffix) {
    RenderRequest request;
    request.source_asset_id = QStringLiteral("display-text-preview-parity");
    request.file_path = source_path;
    request.state = state;
    request.render_reason = QStringLiteral("display_text_overlay_parity");
    request.use_in_memory_preview = true;
    request.allow_native_srgb_surface = allow_native_suffix;
    request.require_cpu_preview_image = !allow_native_suffix;
    request.require_canonical_render_plan = true;
    const auto request_identity = QStringLiteral("display-text-preview-parity-v1:%1:%2")
        .arg(allow_native_suffix ? QStringLiteral("native-request") : QStringLiteral("cpu"))
        .arg(state.layers.isEmpty() ? QStringLiteral("baseline") : QStringLiteral("overlay"));
    request.recipe_cache_identity = request_identity;
    request.adjusted_recipe_cache_identity = request_identity + QStringLiteral(":adjusted");
    request.max_edge = 512;
    request.generation = 1;
    request.latest_generation = std::make_shared<std::atomic<int>>(1);
    const auto admission = RenderPlanStore::shared().admit(
        request.source_asset_id, request.file_path, state, source.size(),
        RenderPurpose::SettledPreview, DisplayRenderingTarget::SrgbSimulation);
    if (admission.admitted) request.canonical_render_plan = admission.plan;
    return request;
  };
  auto native_baseline_request = make_request(baseline_state, true);
  auto cpu_baseline_request = make_request(baseline_state, false);
  auto cpu_overlay_request = make_request(overlay_state, false);
  auto native_overlay_request = make_request(overlay_state, true);
  if (!native_baseline_request.canonical_render_plan.has_value() ||
      !cpu_baseline_request.canonical_render_plan.has_value() ||
      !cpu_overlay_request.canonical_render_plan.has_value() ||
      !native_overlay_request.canonical_render_plan.has_value() ||
      !canonical_native_surface_request_is_eligible(native_baseline_request) ||
      !canonical_native_surface_request_is_eligible(native_overlay_request)) {
    *error = QStringLiteral("The native preview fixture did not qualify for the retained-surface route.");
    return false;
  }

  const auto previously_disabled_metal =
      qEnvironmentVariableIsSet("PHOTO_EDITOR_DISABLE_METAL_PREVIEW");
  const auto previous_metal_value = qgetenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW");
  qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", "1");
  const auto cpu_baseline_result = render_preview_request_for_test(cpu_baseline_request);
  const auto cpu_overlay_result = render_preview_request_for_test(cpu_overlay_request);
  if (previously_disabled_metal) {
    qputenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW", previous_metal_value);
  } else {
    qunsetenv("PHOTO_EDITOR_DISABLE_METAL_PREVIEW");
  }

  const auto native_baseline_result = render_preview_request_for_test(native_baseline_request);
  const auto native_overlay_result = render_preview_request_for_test(native_overlay_request);
  const auto native_baseline_pixels = preview_result_pixels_for_test(native_baseline_result);
  const auto cpu_baseline_pixels = preview_result_pixels_for_test(cpu_baseline_result);
  const auto cpu_overlay_pixels = preview_result_pixels_for_test(cpu_overlay_result);
  const auto native_overlay_pixels = preview_result_pixels_for_test(native_overlay_result);
  const auto cpu_baseline_completed_full_path =
      cpu_baseline_result.completed_render_stages.contains(QStringLiteral("decode")) &&
      cpu_baseline_result.completed_render_stages.contains(QStringLiteral("scene_adjustments")) &&
      cpu_baseline_result.completed_render_stages.contains(QStringLiteral("geometry")) &&
      cpu_baseline_result.completed_render_stages.contains(QStringLiteral("photographic_output")) &&
      cpu_baseline_result.completed_render_stages.contains(QStringLiteral("publish"));
  const auto cpu_overlay_completed_full_path =
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("decode")) &&
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("layers")) &&
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("scene_adjustments")) &&
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("geometry")) &&
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("photographic_output")) &&
      cpu_overlay_result.completed_render_stages.contains(QStringLiteral("publish"));
  const auto native_overlay_completed_suffix =
      native_overlay_result.completed_render_stages.contains(QStringLiteral("layers")) &&
      native_overlay_result.completed_render_stages.contains(QStringLiteral("geometry")) &&
      native_overlay_result.completed_render_stages.contains(QStringLiteral("photographic_output")) &&
      native_overlay_result.completed_render_stages.contains(QStringLiteral("publish"));
  if (!native_baseline_result.success || !cpu_baseline_result.success ||
      !cpu_overlay_result.success || !native_overlay_result.success ||
      native_baseline_result.texture_upload_surface == nullptr ||
      !cpu_baseline_completed_full_path || !cpu_overlay_completed_full_path ||
      !native_overlay_completed_suffix ||
      native_baseline_pixels.isNull() || cpu_baseline_pixels.isNull() ||
      cpu_overlay_pixels.isNull() || native_overlay_pixels.isNull() ||
      native_overlay_result.texture_upload_surface != nullptr) {
    *error = QStringLiteral(
        "The controller preview did not publish the matched CPU pair and native-request route as expected. "
        "nativeBaseline(success=%1, backend=%2, stages=%3, image=%4, surface=%5, size=%6x%7); "
        "cpuBaseline(success=%8, backend=%9, stages=%10, image=%11); "
        "cpuOverlay(success=%12, backend=%13, stages=%14, image=%15); "
        "nativeOverlay(success=%16, failure=%17/%18, backend=%19, stages=%20, image=%21, surface=%22).")
        .arg(native_baseline_result.success ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_baseline_result.backend)
        .arg(native_baseline_result.completed_render_stages.join(QLatin1Char(',')))
        .arg(!native_baseline_result.preview_image.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_baseline_result.texture_upload_surface != nullptr ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_baseline_result.output_width)
        .arg(native_baseline_result.output_height)
        .arg(cpu_baseline_result.success ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(cpu_baseline_result.backend)
        .arg(cpu_baseline_result.completed_render_stages.join(QLatin1Char(',')))
        .arg(!cpu_baseline_result.preview_image.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(cpu_overlay_result.success ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(cpu_overlay_result.backend)
        .arg(cpu_overlay_result.completed_render_stages.join(QLatin1Char(',')))
        .arg(!cpu_overlay_result.preview_image.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_overlay_result.success ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_overlay_result.failure_stage)
        .arg(native_overlay_result.failure_detail)
        .arg(native_overlay_result.backend)
        .arg(native_overlay_result.completed_render_stages.join(QLatin1Char(',')))
        .arg(!native_overlay_result.preview_image.isNull() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(native_overlay_result.texture_upload_surface != nullptr ? QStringLiteral("true") : QStringLiteral("false"));
    return false;
  }

  QPoint first_cross_route_delta;
  const auto cross_route_delta = maximum_channel_delta(
      native_baseline_pixels, cpu_baseline_pixels, &first_cross_route_delta);
  if (cross_route_delta > 1) {
    const auto native_pixel = native_baseline_pixels.pixelColor(first_cross_route_delta);
    const auto cpu_pixel = cpu_baseline_pixels.pixelColor(first_cross_route_delta);
    *error = QStringLiteral(
        "The native baseline surface and CPU baseline exceed the one-LSB cross-publication allowance "
        "at (%1,%2): delta=%3, native RGB=(%4,%5,%6), CPU RGB=(%7,%8,%9).")
        .arg(first_cross_route_delta.x()).arg(first_cross_route_delta.y()).arg(cross_route_delta)
        .arg(native_pixel.red()).arg(native_pixel.green()).arg(native_pixel.blue())
        .arg(cpu_pixel.red()).arg(cpu_pixel.green()).arg(cpu_pixel.blue());
    return false;
  }
  if (cross_route_delta > 0) {
    const auto native_pixel = native_baseline_pixels.pixelColor(first_cross_route_delta);
    const auto cpu_pixel = cpu_baseline_pixels.pixelColor(first_cross_route_delta);
    std::cout << "Native surface baseline vs Metal-disabled CPU baseline: max channel delta="
              << cross_route_delta << " at (" << first_cross_route_delta.x() << ','
              << first_cross_route_delta.y() << "), native RGB=(" << native_pixel.red() << ','
              << native_pixel.green() << ',' << native_pixel.blue() << "), CPU RGB=("
              << cpu_pixel.red() << ',' << cpu_pixel.green() << ',' << cpu_pixel.blue()
              << "); baseline publication storage=MetalOutputSurface to QImage on CPU route.\n";
  }
  const auto route_context = QStringLiteral(
      "Metal preview disabled for both requests; baseline backend=%1 stages=%2; overlay backend=%3 stages=%4")
      .arg(cpu_baseline_result.backend)
      .arg(cpu_baseline_result.completed_render_stages.join(QLatin1Char(',')))
      .arg(cpu_overlay_result.backend)
      .arg(cpu_overlay_result.completed_render_stages.join(QLatin1Char(',')));
  if (!verify_overlay_pixels(
      cpu_baseline_pixels, cpu_overlay_pixels,
      overlay_layer, QColor(QStringLiteral("#ffffff")), error, route_context)) {
    return false;
  }
  const auto exported = render_output_image_with_backend(
      source, overlay_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  if (exported.isNull() || !same_image_pixels(native_overlay_pixels, exported)) {
    *error = QStringLiteral("The native-surface-eligible preview suffix did not match the sRGB export pixels exactly.");
    return false;
  }
  return true;
}

QColorSpace icc_color_space(const ResolvedIccProfileAsset& profile) {
  const auto bytes = profile.payload_bytes();
  return QColorSpace::fromIccProfile(QByteArray(
      reinterpret_cast<const char*>(bytes.data()),
      static_cast<qsizetype>(bytes.size())));
}

std::array<double, 3> encoded_srgb_to_linear_rec2020(const QColor& encoded) {
  const auto transform = compile_builtin_color_transform(
      make_linear_srgb_color_space(), make_linear_rec2020_color_space());
  const auto linear_srgb = std::array<double, 3>{
      srgb_to_linear(encoded.redF()),
      srgb_to_linear(encoded.greenF()),
      srgb_to_linear(encoded.blueF()),
  };
  return multiply_matrix3_vector(transform.matrix, linear_srgb);
}

IccRgbaFloat to_linear_rec2020(
    const QImage& image,
    int x,
    int y,
    const CompiledIccTransform& device_to_linear_rec2020) {
  const auto pixel = image.pixel(x, y);
  std::array<IccRgbaFloat, 1> values{{
      IccRgbaFloat{
          .red = static_cast<float>(qRed(pixel) / 255.0),
          .green = static_cast<float>(qGreen(pixel) / 255.0),
          .blue = static_cast<float>(qBlue(pixel) / 255.0),
          .alpha = 1.0F,
      },
  }};
  if (!device_to_linear_rec2020.apply(values)) return {};
  return values.front();
}

IccRgbaFloat to_device_encoded(
    const std::array<double, 3>& linear_rec2020,
    const CompiledIccTransform& linear_rec2020_to_device) {
  std::array<IccRgbaFloat, 1> values{{
      IccRgbaFloat{
          .red = static_cast<float>(linear_rec2020[0]),
          .green = static_cast<float>(linear_rec2020[1]),
          .blue = static_cast<float>(linear_rec2020[2]),
          .alpha = 1.0F,
      },
  }};
  if (!linear_rec2020_to_device.apply(values)) return {};
  return values.front();
}

bool verify_monitor_icc_overlay(
    const QImage& baseline,
    const QImage& rendered,
    const PhotoLayer& layer,
    const QColor& authored_color,
    const CompiledIccTransform& device_to_linear_rec2020,
    const CompiledIccTransform& linear_rec2020_to_device,
    bool require_opaque_and_edge_pixels,
    std::optional<std::size_t> required_masked_edge_index,
    QString* error) {
  if (baseline.isNull() || rendered.isNull() ||
      baseline.size() != rendered.size() ||
      baseline.format() != QImage::Format_ARGB32 ||
      rendered.format() != QImage::Format_ARGB32 ||
      layer.pixel_width != baseline.width() ||
      layer.pixel_height != baseline.height() ||
      layer.pixels.size() != static_cast<std::size_t>(baseline.width() * baseline.height()) ||
      (!layer.mask_data.empty() &&
       (layer.mask_width != baseline.width() ||
        layer.mask_height != baseline.height() ||
        layer.mask_data.size() != static_cast<std::size_t>(baseline.width() * baseline.height())))) {
    *error = QStringLiteral("The monitor ICC fixture did not produce matching ARGB32 device renders.");
    return false;
  }
  const auto authored_linear = encoded_srgb_to_linear_rec2020(authored_color);
  auto found_opaque = false;
  auto found_edge = false;
  auto checked_required_masked_edge = !required_masked_edge_index.has_value();
  for (int y = 0; y < baseline.height(); ++y) {
    for (int x = 0; x < baseline.width(); ++x) {
      const auto index = static_cast<std::size_t>(y * baseline.width() + x);
      const auto& source_ink = layer.pixels[index];
      const auto glyph_alpha = std::clamp(static_cast<double>(source_ink.alpha), 0.0, 1.0);
      const auto mask_coverage = layer.mask_data.empty()
          ? 1.0
          : static_cast<double>(layer.mask_data[index]) / 255.0;
      const auto effective_alpha = std::clamp(
          glyph_alpha * std::clamp(layer.opacity, 0.0, 1.0) *
              mask_coverage * std::clamp(layer.mask_opacity, 0.0, 1.0),
          0.0, 1.0);
      if (effective_alpha <= 1.0e-7) {
        if (!same_pixel(baseline, rendered, x, y)) {
          *error = QStringLiteral("The monitor ICC suffix changed a pixel outside masked text coverage.");
          return false;
        }
        continue;
      }
      const auto background = to_linear_rec2020(
          baseline, x, y, device_to_linear_rec2020);
      const std::array<double, 3> composited{
          effective_alpha * authored_linear[0] + (1.0 - effective_alpha) * background.red,
          effective_alpha * authored_linear[1] + (1.0 - effective_alpha) * background.green,
          effective_alpha * authored_linear[2] + (1.0 - effective_alpha) * background.blue,
      };
      const auto expected = to_device_encoded(composited, linear_rec2020_to_device);
      const auto actual = rendered.pixel(x, y);
      if (!std::isfinite(expected.red) || !std::isfinite(expected.green) ||
          !std::isfinite(expected.blue) || qAlpha(actual) != qAlpha(baseline.pixel(x, y))) {
        *error = QStringLiteral("The monitor ICC suffix produced non-finite ink or changed opaque source alpha.");
        return false;
      }
      const auto channel_delta = std::max({
          std::abs(qRed(actual) - qRound(std::clamp<double>(expected.red, 0.0, 1.0) * 255.0)),
          std::abs(qGreen(actual) - qRound(std::clamp<double>(expected.green, 0.0, 1.0) * 255.0)),
          std::abs(qBlue(actual) - qRound(std::clamp<double>(expected.blue, 0.0, 1.0) * 255.0)),
      });
      if (channel_delta > 5) {
        *error = QStringLiteral("Monitor ICC pixel (%1,%2) diverged: glyphAlpha=%3 layerOpacity=%4 mask=%5 maskOpacity=%6 alpha=%7 ink=(%8,%9,%10) baseline=(%11,%12,%13) actual=(%14,%15,%16) expected=(%17,%18,%19) linearBackground=(%20,%21,%22).")
            .arg(x).arg(y).arg(glyph_alpha, 0, 'f', 4).arg(layer.opacity, 0, 'f', 4)
            .arg(mask_coverage, 0, 'f', 4).arg(layer.mask_opacity, 0, 'f', 4)
            .arg(effective_alpha, 0, 'f', 4)
            .arg(source_ink.red, 0, 'f', 3).arg(source_ink.green, 0, 'f', 3)
            .arg(source_ink.blue, 0, 'f', 3)
            .arg(qRed(baseline.pixel(x, y))).arg(qGreen(baseline.pixel(x, y))).arg(qBlue(baseline.pixel(x, y)))
            .arg(qRed(actual)).arg(qGreen(actual)).arg(qBlue(actual))
            .arg(qRound(std::clamp<double>(expected.red, 0.0, 1.0) * 255.0))
            .arg(qRound(std::clamp<double>(expected.green, 0.0, 1.0) * 255.0))
            .arg(qRound(std::clamp<double>(expected.blue, 0.0, 1.0) * 255.0))
            .arg(background.red, 0, 'f', 4).arg(background.green, 0, 'f', 4)
            .arg(background.blue, 0, 'f', 4);
        return false;
      }
      if (effective_alpha >= 0.995) found_opaque = true;
      if (effective_alpha >= 0.15 && effective_alpha <= 0.85) found_edge = true;
      if (required_masked_edge_index == index) {
        if (channel_delta > 5 || same_pixel(baseline, rendered, x, y)) {
          *error = QStringLiteral("The direct full-canvas mask did not attenuate its selected antialiased glyph pixel through the device ICC route.");
          return false;
        }
        checked_required_masked_edge = true;
      }
    }
  }
  if ((require_opaque_and_edge_pixels && (!found_opaque || !found_edge)) ||
      !checked_required_masked_edge) {
    *error = QStringLiteral("The monitor ICC text fixture did not exercise the required opaque, antialiased, and mask samples.");
    return false;
  }
  return true;
}

bool verify_system_monitor_icc_preview(
    const QImage& source,
    const QColor& authored_color,
    bool* skipped,
    QString* error) {
  const auto system_profile = refresh_system_monitor_profile_for_test();
  if (!system_profile.valid()) {
    if (skipped != nullptr) *skipped = true;
    *error = QStringLiteral("No system-monitor ICC profile is available in this runtime.");
    return false;
  }

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    *error = QStringLiteral("Could not create the isolated system ICC preview runtime.");
    return false;
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  if (!AppBootstrap::ensureRuntimeDirectories()) {
    *error = QStringLiteral("Could not initialize the isolated system ICC preview runtime.");
    return false;
  }
  const auto source_path = runtime_root.filePath(QStringLiteral("monitor-icc-source.png"));
  if (!source.save(source_path)) {
    *error = QStringLiteral("Could not write the system ICC preview fixture.");
    return false;
  }

  EditRecipeState baseline_state;
  baseline_state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  baseline_state.preview_color_space = QStringLiteral("sRGB");
  baseline_state.preview_output_mode = QStringLiteral("Monitor");
  baseline_state.monitor_color_profile.clear();
  baseline_state.soft_proof_enabled = false;
  const auto color_pipeline = compile_preview_color_pipeline_config_for_display(
      baseline_state, nullptr, system_profile);
  if (!color_pipeline.preview_icc_profile_asset.ready()) {
    *error = QStringLiteral("The discovered system monitor ICC profile did not resolve as a ready preview asset.");
    return false;
  }
  const auto& monitor_reference = color_pipeline.preview_icc_profile_asset.reference;
  baseline_state.preview_icc_profile_asset = IccProfileAssetState{
      .asset_id = QString::fromStdString(monitor_reference.asset_id),
      .payload_sha256 = QString::fromStdString(monitor_reference.payload_sha256),
      .display_name = QString::fromStdString(monitor_reference.display_name),
      .source_uri = QString::fromStdString(monitor_reference.source_uri),
      .source_kind = QString::fromStdString(monitor_reference.source_kind),
      .byte_size = static_cast<qint64>(monitor_reference.byte_size),
  };
  // The test request mirrors the controller's settled photo state, which has
  // visible edits and carries the resolved active monitor asset into admission.
  baseline_state.exposure = 0.25;

  const auto rec2020_profile = builtin_rgb_icc_profile_payload(
      make_linear_rec2020_color_space());
  const auto profile_payload = color_pipeline.preview_icc_profile_asset.payload_bytes();
  const auto icc_options = preview_device_icc_options(color_pipeline, true);
  const auto linear_rec2020_to_device = compile_icc_transform(
      rec2020_profile, profile_payload, icc_options);
  auto inverse_options = icc_options;
  inverse_options.target = "monitor-test:device-to-linear-rec2020";
  const auto device_to_linear_rec2020 = compile_icc_transform(
      profile_payload, rec2020_profile, inverse_options);
  if (rec2020_profile.empty() || !linear_rec2020_to_device.ready() ||
      !device_to_linear_rec2020.ready()) {
    *error = QStringLiteral("The system monitor profile could not compile both directions of the preview ICC transform.");
    return false;
  }

  const auto configure = [](EditRecipeState state) {
    state.preview_color_space = QStringLiteral("sRGB");
    state.preview_output_mode = QStringLiteral("Monitor");
    state.monitor_color_profile.clear();
    state.soft_proof_enabled = false;
    return state;
  };
  baseline_state = configure(std::move(baseline_state));
  auto overlay_state = baseline_state;
  auto text_layer = plain_text_layer(QStringLiteral("monitor-icc-text"), QStringLiteral("INK"));
  text_layer.insert(QStringLiteral("textColor"), authored_color.name(QColor::HexRgb));
  overlay_state.layers = QVariantList{text_layer};
  const auto compiled_overlay = compile_desktop_pipeline_state(
      overlay_state, nullptr, color_pipeline, DesktopRenderPurpose::Preview,
      LocalMaskRasterPolicy::UseStoredRaster, source.size());
  if (compiled_overlay.display_referred_text_layers.size() != 1 ||
      !compiled_overlay.photo_layers.empty()) {
    *error = QStringLiteral("The actual system monitor profile did not classify plain root text for the device ICC suffix.");
    return false;
  }
  const auto text_layers = compile_pipeline_photo_layers(
      overlay_state.layers, overlay_state.mask_stack,
      LocalMaskRasterPolicy::UseStoredRaster, source.size(), {},
      color_pipeline, DesktopRenderPurpose::Preview);
  if (text_layers.size() != 1) {
    *error = QStringLiteral("Could not compile the monitor ICC glyph alpha fixture.");
    return false;
  }

  const auto make_request = [&](const EditRecipeState& state) {
    RenderRequest request;
    request.source_asset_id = QStringLiteral("display-text-system-monitor-icc");
    request.file_path = source_path;
    request.state = state;
    request.render_reason = QStringLiteral("display_text_system_monitor_icc");
    request.use_in_memory_preview = true;
    request.allow_native_srgb_surface = false;
    request.require_cpu_preview_image = true;
    request.require_canonical_render_plan = true;
    request.max_edge = 512;
    request.generation = 1;
    request.latest_generation = std::make_shared<std::atomic<int>>(1);
    const auto admission = RenderPlanStore::shared().admit(
        request.source_asset_id, request.file_path, state, source.size(),
        RenderPurpose::SettledPreview, DisplayRenderingTarget::ActiveDisplay,
        AssetIdentifier{color_pipeline.preview_icc_profile_asset.reference.asset_id});
    if (admission.admitted) request.canonical_render_plan = admission.plan;
    return request;
  };
  const auto baseline_request = make_request(baseline_state);
  const auto overlay_request = make_request(overlay_state);
  if (!baseline_request.canonical_render_plan.has_value() ||
      !overlay_request.canonical_render_plan.has_value()) {
    *error = QStringLiteral("The system monitor ICC render plans were not admitted.");
    return false;
  }
  const auto baseline_result = render_preview_request_for_test(baseline_request);
  const auto overlay_result = render_preview_request_for_test(overlay_request);
  const auto expected_space = icc_color_space(color_pipeline.preview_icc_profile_asset);
  const auto has_device_stage = [](const RenderResult& result) {
    return result.completed_render_stages.contains(QStringLiteral("device_transform")) &&
        result.completed_render_stages.contains(QStringLiteral("publish"));
  };
  if (!baseline_result.success || !overlay_result.success ||
      baseline_result.preview_image.isNull() || overlay_result.preview_image.isNull() ||
      !has_device_stage(baseline_result) || !has_device_stage(overlay_result) ||
      !expected_space.isValid() ||
      baseline_result.preview_image.colorSpace() != expected_space ||
      overlay_result.preview_image.colorSpace() != expected_space) {
    const auto describe = [](const RenderResult& result) {
      return QStringLiteral("success=%1 status=%2 failedStage=%3 detail=%4 stages=%5 image=%6 format=%7 tagged=%8")
          .arg(result.success)
          .arg(result.status)
          .arg(result.failure_stage)
          .arg(result.failure_detail)
          .arg(result.completed_render_stages.join(QLatin1Char(',')))
          .arg(!result.preview_image.isNull())
          .arg(static_cast<int>(result.preview_image.format()))
          .arg(result.preview_image.colorSpace().isValid());
    };
    *error = QStringLiteral("The actual preview controller did not publish through the ready system monitor DeviceTransform and ICC-tagged Publish stages (expectedProfile=%1; baseline {%2}; text {%3}).")
        .arg(expected_space.isValid())
        .arg(describe(baseline_result), describe(overlay_result));
    return false;
  }
  if (!verify_monitor_icc_overlay(
          baseline_result.preview_image, overlay_result.preview_image,
          text_layers.front(), authored_color, device_to_linear_rec2020,
          linear_rec2020_to_device, true, std::nullopt, error)) {
    return false;
  }

  const auto unmasked_layer = text_layers.front();
  const auto masked_edge = std::find_if(
      unmasked_layer.pixels.cbegin(), unmasked_layer.pixels.cend(),
      [](const ColorPixel& pixel) {
        return pixel.alpha >= 0.35F && pixel.alpha <= 0.65F;
      });
  if (masked_edge == unmasked_layer.pixels.cend()) {
    *error = QStringLiteral("The monitor ICC glyph did not contain an antialiased mask sample.");
    return false;
  }
  const auto mask_index = static_cast<std::size_t>(
      std::distance(unmasked_layer.pixels.cbegin(), masked_edge));
  auto mask = QByteArray(source.width() * source.height(), static_cast<char>(0xff));
  mask[static_cast<qsizetype>(mask_index)] = static_cast<char>(0x80);
  auto masked_state = overlay_state;
  text_layer.insert(QStringLiteral("opacity"), 0.82);
  text_layer.insert(QStringLiteral("maskData"), mask);
  text_layer.insert(QStringLiteral("maskWidth"), source.width());
  text_layer.insert(QStringLiteral("maskHeight"), source.height());
  text_layer.insert(QStringLiteral("maskOpacity"), 0.72);
  masked_state.layers = QVariantList{text_layer};
  const auto masked_compiled = compile_desktop_pipeline_state(
      masked_state, nullptr, color_pipeline, DesktopRenderPurpose::Preview,
      LocalMaskRasterPolicy::UseStoredRaster, source.size());
  const auto masked_layers = compile_pipeline_photo_layers(
      masked_state.layers, masked_state.mask_stack,
      LocalMaskRasterPolicy::UseStoredRaster, source.size(), {},
      color_pipeline, DesktopRenderPurpose::Preview);
  if (masked_compiled.display_referred_text_layers.size() != 1 ||
      !masked_compiled.photo_layers.empty() || masked_layers.size() != 1) {
    *error = QStringLiteral("A same-aspect full-canvas own mask should remain eligible through the actual monitor ICC route.");
    return false;
  }
  const auto masked_request = make_request(masked_state);
  if (!masked_request.canonical_render_plan.has_value()) {
    *error = QStringLiteral("The masked system monitor ICC render plan was not admitted.");
    return false;
  }
  const auto masked_result = render_preview_request_for_test(masked_request);
  if (!masked_result.success || masked_result.preview_image.isNull() ||
      !has_device_stage(masked_result) ||
      !verify_monitor_icc_overlay(
          baseline_result.preview_image, masked_result.preview_image,
          masked_layers.front(), authored_color, device_to_linear_rec2020,
          linear_rec2020_to_device, false, mask_index, error)) {
    if (error->isEmpty())
      *error = QStringLiteral("The masked system monitor ICC preview failed before publication.");
    return false;
  }
  return true;
}

bool verify_task_owned_photo_fragment_file_effects(QString* error) {
  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    *error = QStringLiteral("Could not create isolated Photo Fragment ownership runtime.");
    return false;
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoDisplayTextOverlayTest"));
  if (!AppBootstrap::ensureRuntimeDirectories() ||
      !QDir().mkpath(runtime_root.filePath(QStringLiteral("fixtures"))) ||
      !QDir().mkpath(runtime_root.filePath(QStringLiteral("exports")))) {
    *error = QStringLiteral("Could not initialize isolated Photo Fragment ownership directories.");
    return false;
  }

  const QString guarded_preset_name = QStringLiteral("Task Guard Existing");
  QImage baseline_tip(3, 3, QImage::Format_ARGB32);
  baseline_tip.fill(QColor(QStringLiteral("#ff2455")));
  QImage imported_tip(3, 3, QImage::Format_ARGB32);
  imported_tip.fill(QColor(QStringLiteral("#1255e8")));
  if (!PhotoFragmentBrushPresetStore::save(guarded_preset_name, baseline_tip, 0.0, 0.0, true)) {
    *error = QStringLiteral("Prepare the isolated Photo Fragment ownership fixture.");
    return false;
  }
  QByteArray imported_png;
  QBuffer imported_buffer(&imported_png);
  if (!imported_buffer.open(QIODevice::WriteOnly) || !imported_tip.save(&imported_buffer, "PNG")) {
    *error = QStringLiteral("Encode a valid alternate Photo Fragment import fixture.");
    return false;
  }
  const auto import_fixture_path =
      runtime_root.filePath(QStringLiteral("fixtures/task-guard-import.json"));
  QFile import_fixture(import_fixture_path);
  if (!import_fixture.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      import_fixture.write(QJsonDocument(QJsonObject{
          {QStringLiteral("version"), 1}, {QStringLiteral("name"), guarded_preset_name},
          {QStringLiteral("imagePngBase64"), QString::fromLatin1(imported_png.toBase64())},
          {QStringLiteral("rotationVariation"), 0.0}, {QStringLiteral("scaleVariation"), 0.0},
      }).toJson(QJsonDocument::Compact)) <= 0) {
    *error = QStringLiteral("Write the valid alternate Photo Fragment import fixture.");
    return false;
  }
  import_fixture.close();

  const auto source_path = runtime_root.filePath(QStringLiteral("fixtures/task-guard-source.png"));
  QImage task_source(24, 16, QImage::Format_ARGB32);
  task_source.fill(QColor::fromRgb(86, 104, 122));
  if (!task_source.save(source_path)) {
    *error = QStringLiteral("Write a native editor source for the Photo Fragment ownership contract.");
    return false;
  }
  EditRecipeState task_state;
  task_state.monitor_color_profile = QStringLiteral("sRGB");
  TestEditorPreviewImageSink task_image_provider;
  EditorPreviewController task_preview;
  task_preview.setImageProvider(&task_image_provider);
  task_preview.setScopesVisible(false);
  task_preview.loadAsset(QStringLiteral("asset-1"), source_path, {}, {}, {}, {},
      serialize_edit_recipe_json(task_state));
  QElapsedTimer preview_timer;
  preview_timer.start();
  while (task_image_provider.image(QStringLiteral("edited")).isNull() &&
         task_preview.previewFailureStage().isEmpty() && preview_timer.elapsed() < 30000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  const auto loaded_preview = task_image_provider.image(QStringLiteral("edited"));
  if (loaded_preview.isNull()) {
    *error = QStringLiteral(
        "Load a native editor document for the file-effect ownership contract. status=%1, "
        "failureStage=%2, failureDetail=%3, sourceExists=%4, processing=%5")
        .arg(task_preview.status(), task_preview.previewFailureStage(),
             task_preview.previewFailureDetail(),
             QFileInfo::exists(source_path) ? QStringLiteral("true") : QStringLiteral("false"),
             task_preview.processing() ? QStringLiteral("true") : QStringLiteral("false"));
    return false;
  }
  const QString task_id = QStringLiteral("photo-fragment-file-effect-guard");
  if (!task_preview.beginAgentTask(task_id).value(QStringLiteral("ok")).toBool()) {
    *error = QStringLiteral("Capture an owned native recipe before testing Photo Fragment file effects.");
    return false;
  }
  const auto before_recipe = task_preview.currentAssetBoundAdjustmentPatchJson();
  const auto task_export_path =
      runtime_root.filePath(QStringLiteral("exports/task-guard-export.json"));
  const auto denied_file_effects = QVariantList{
      QVariantMap{{QStringLiteral("operation"), QStringLiteral("save_photo_fragment_preset")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("name"), QStringLiteral("Task Guard New")},
              {QStringLiteral("sourcePresetName"), guarded_preset_name}}}},
      QVariantMap{{QStringLiteral("operation"), QStringLiteral("delete_photo_fragment_preset")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("name"), guarded_preset_name}}}},
      QVariantMap{{QStringLiteral("operation"), QStringLiteral("import_photo_fragment_preset")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("path"), import_fixture_path},
              {QStringLiteral("overwrite"), true}}}},
      QVariantMap{{QStringLiteral("operation"), QStringLiteral("export_photo_fragment_preset")},
          {QStringLiteral("values"), QVariantMap{{QStringLiteral("name"), guarded_preset_name},
              {QStringLiteral("path"), task_export_path}}}},
  };
  for (const auto& request_value : denied_file_effects) {
    auto request = request_value.toMap();
    request.insert(QStringLiteral("assetId"), QStringLiteral("asset-1"));
    const auto result = task_preview.runAgentTaskComposition(task_id, request);
    const auto task_state = task_preview.agentTaskState();
    const auto stored_tip = PhotoFragmentBrushPresetStore::imageFromPreset(
        PhotoFragmentBrushPresetStore::load(guarded_preset_name));
    if (result.value(QStringLiteral("ok")).toBool() ||
        !result.value(QStringLiteral("blockedByResourceOwnership")).toBool() ||
        !result.value(QStringLiteral("mutationNotApplied")).toBool() ||
        task_state.value(QStringLiteral("steps")).toList().size() != 0 ||
        task_preview.currentAssetBoundAdjustmentPatchJson() != before_recipe ||
        !PhotoFragmentBrushPresetStore::names().contains(guarded_preset_name) ||
        PhotoFragmentBrushPresetStore::names().contains(QStringLiteral("Task Guard New")) ||
        stored_tip != baseline_tip || QFileInfo::exists(task_export_path)) {
      *error = QStringLiteral(
          "An unowned Photo Fragment file effect must be rejected before touching files or the task journal.");
      return false;
    }
  }
  const auto owned_inspect = task_preview.runAgentTaskComposition(task_id,
      {{QStringLiteral("assetId"), QStringLiteral("asset-1")},
       {QStringLiteral("operation"), QStringLiteral("inspect")}});
  if (!owned_inspect.value(QStringLiteral("ok")).toBool() ||
      !owned_inspect.value(QStringLiteral("photoFragmentBrushPresets")).toStringList().contains(guarded_preset_name) ||
      !task_preview.agentTaskState().value(QStringLiteral("steps")).toList().isEmpty()) {
    *error = QStringLiteral(
        "Read-only composition inspection must remain available without creating a task mutation step.");
    return false;
  }
  const auto owned_text = task_preview.runAgentTaskComposition(task_id,
      {{QStringLiteral("assetId"), QStringLiteral("asset-1")},
       {QStringLiteral("operation"), QStringLiteral("add_text")},
       {QStringLiteral("values"), QVariantMap{{QStringLiteral("text"), QStringLiteral("Owned title")},
           {QStringLiteral("x"), 24}, {QStringLiteral("y"), 24}}}});
  if (!owned_text.value(QStringLiteral("ok")).toBool() ||
      !owned_text.value(QStringLiteral("changed")).toBool() ||
      task_preview.agentTaskState().value(QStringLiteral("steps")).toList().size() != 1 ||
      !task_preview.revertAgentTask(task_id).value(QStringLiteral("ok")).toBool() ||
      task_preview.currentAssetBoundAdjustmentPatchJson() != before_recipe) {
    *error = QStringLiteral("Recipe-owned composition edits must remain admitted and exactly task-revertible.");
    return false;
  }
  const auto direct_export_path =
      runtime_root.filePath(QStringLiteral("exports/direct-user-export.json"));
  const auto direct_export = task_preview.photoCompositionCommand({
      {QStringLiteral("assetId"), QStringLiteral("asset-1")},
      {QStringLiteral("operation"), QStringLiteral("export_photo_fragment_preset")},
      {QStringLiteral("values"), QVariantMap{{QStringLiteral("name"), guarded_preset_name},
          {QStringLiteral("path"), direct_export_path}}}});
  if (!direct_export.value(QStringLiteral("ok")).toBool() ||
      !QFileInfo::exists(direct_export_path)) {
    *error = QStringLiteral(
        "The direct editor Photo Fragment export flow must remain available outside the task-owned route.");
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  const auto source = sdr_source();
  EditRecipeState baseline_state;
  baseline_state.photographic_rendering_model = QStringLiteral("aces2-ocio-v1");
  EditRecipeState overlay_state = baseline_state;
  overlay_state.layers = QVariantList{plain_text_layer(QStringLiteral("cover-title"))};
  const auto overlay_config = compile_color_pipeline_config(overlay_state);
  const auto compiled_overlay_state = compile_state(overlay_state, source.size(), overlay_config);
  if (compiled_overlay_state.display_referred_text_layers.size() != 1 ||
      compiled_overlay_state.display_referred_text_layers.front().id != "cover-title" ||
      has_layer_id(compiled_overlay_state.photo_layers, QStringLiteral("cover-title"))) {
    std::cerr << "A plain topmost root text layer must move into the display-referred overlay stage\n";
    return 1;
  }
  const auto base_admission = RenderPlanStore::shared().admit(
      QStringLiteral("display-text-cache-identity"),
      QStringLiteral("synthetic-display-text-overlay"), baseline_state, source.size(),
      RenderPurpose::Export, DisplayRenderingTarget::SrgbSimulation);
  const auto text_admission = RenderPlanStore::shared().admit(
      QStringLiteral("display-text-cache-identity"),
      QStringLiteral("synthetic-display-text-overlay"), overlay_state, source.size(),
      RenderPurpose::Export, DisplayRenderingTarget::SrgbSimulation);
  const auto baseline = base_admission.admitted && base_admission.plan.has_value()
      ? render_with_plan(source, baseline_state, *base_admission.plan)
      : QImage{};
  if (baseline.isNull()) {
    std::cerr << "Could not render the ACES2 baseline through the admitted canonical output plan\n";
    return 1;
  }
  const auto has_versioned_output_stage = [](const std::optional<RenderPlan>& plan) {
    if (!plan.has_value()) return false;
    const auto stage = std::find_if(plan->stages.cbegin(), plan->stages.cend(),
        [](const RenderStagePlan& candidate) {
          return candidate.kind == RenderStageKind::PhotographicOutput;
        });
    return stage != plan->stages.cend() &&
        stage->cache_identity.starts_with(
            "photographic-output-aces2-v6-display-text-policy-fnv1a64-");
  };
  if (!base_admission.admitted || !text_admission.admitted ||
      base_admission.plan_cache_key == text_admission.plan_cache_key ||
      !has_versioned_output_stage(base_admission.plan) ||
      !has_versioned_output_stage(text_admission.plan)) {
    std::cerr << "Display text policy must version the PhotographicOutput cache identity and separate changed recipes\n";
    return 1;
  }
  const auto compiled_overlay_layers = compile_all_layers(overlay_state, overlay_config);
  if (compiled_overlay_layers.size() != 1 || compiled_overlay_layers.front().id != "cover-title") {
    std::cerr << "The text alpha fixture could not be compiled\n";
    return 1;
  }
  const auto rendered_overlay = text_admission.admitted && text_admission.plan.has_value()
      ? render_with_plan(source, overlay_state, *text_admission.plan)
      : QImage{};
  QString overlay_error;
  if (rendered_overlay.isNull() ||
      !verify_overlay_pixels(baseline, rendered_overlay, compiled_overlay_layers.front(),
          QColor(QStringLiteral("#ffffff")), &overlay_error)) {
    std::cerr << overlay_error.toStdString() << '\n';
    return 1;
  }

  auto warm_text = plain_text_layer(QStringLiteral("warm-title"), QStringLiteral("WARM"));
  warm_text.insert(QStringLiteral("textColor"), QStringLiteral("#f2ebdc"));
  auto warm_state = baseline_state;
  warm_state.layers = QVariantList{warm_text};
  const auto warm_config = compile_color_pipeline_config(warm_state);
  const auto warm_compiled_state = compile_state(warm_state, source.size(), warm_config);
  const auto warm_layers = compile_all_layers(warm_state, warm_config);
  const auto warm_plan = admitted_plan(
      QStringLiteral("display-text-warm"), warm_state, source.size(),
      DisplayRenderingTarget::SrgbSimulation);
  const auto warm_rendered = warm_plan.has_value()
      ? render_with_plan(source, warm_state, *warm_plan)
      : QImage{};
  QString warm_error;
  if (warm_compiled_state.display_referred_text_layers.size() != 1 ||
      warm_layers.size() != 1 || warm_rendered.isNull() ||
      !verify_overlay_pixels(baseline, warm_rendered, warm_layers.front(),
          QColor(QStringLiteral("#f2ebdc")), &warm_error)) {
    std::cerr << (warm_error.isEmpty()
        ? "Warm authored text did not enter the display-referred overlay stage\n"
        : warm_error.toStdString() + '\n');
    return 1;
  }

  auto stack_bottom = plain_text_layer(QStringLiteral("stack-bottom"), QStringLiteral("STACK"));
  stack_bottom.insert(QStringLiteral("textColor"), QStringLiteral("#d8e4f0"));
  stack_bottom.insert(QStringLiteral("opacity"), 0.85);
  auto stack_middle = plain_text_layer(QStringLiteral("stack-middle"), QStringLiteral("STACK"));
  stack_middle.insert(QStringLiteral("opacity"), 0.65);
  stack_middle.insert(QStringLiteral("maskData"), QByteArray(
      kWidth * kHeight, static_cast<char>(0xff)));
  stack_middle.insert(QStringLiteral("maskWidth"), kWidth);
  stack_middle.insert(QStringLiteral("maskHeight"), kHeight);
  stack_middle.insert(QStringLiteral("maskOpacity"), 0.5);
  auto stack_top = plain_text_layer(QStringLiteral("stack-top"), QStringLiteral("STACK"));
  stack_top.insert(QStringLiteral("textColor"), QStringLiteral("#f2ebdc"));
  stack_top.insert(QStringLiteral("opacity"), 0.45);
  auto overlap_state = baseline_state;
  // Desktop rows are top first; the compiled compositor and overlay vector
  // retain the corresponding bottom-to-top accumulation order.
  overlap_state.layers = QVariantList{stack_top, stack_middle, stack_bottom};
  const auto overlap_config = compile_color_pipeline_config(overlap_state);
  const auto overlap_compiled = compile_state(overlap_state, source.size(), overlap_config);
  const auto& overlap_layers = overlap_compiled.display_referred_text_layers;
  if (overlap_layers.size() != 3 ||
      overlap_layers[0].id != "stack-bottom" || overlap_layers[1].id != "stack-middle" ||
      overlap_layers[2].id != "stack-top" || overlap_layers[0].opacity != 0.85 ||
      overlap_layers[1].opacity != 0.65 || overlap_layers[2].opacity != 0.45 ||
      overlap_layers[1].mask_width != kWidth || overlap_layers[1].mask_height != kHeight ||
      overlap_layers[1].mask_data.size() != static_cast<std::size_t>(kWidth * kHeight) ||
      !std::all_of(overlap_layers[1].mask_data.cbegin(), overlap_layers[1].mask_data.cend(),
          [](std::uint8_t coverage) { return coverage == 0xffU; }) ||
      overlap_layers[1].mask_opacity != 0.5 || !overlap_compiled.photo_layers.empty()) {
    std::cerr << "The display overlay must preserve bottom-to-top order, layer opacity and full-size own masks\n";
    return 1;
  }
  const auto overlap_rendered = render_output_image_with_backend(
      source, overlap_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  if (overlap_rendered.isNull() || same_image_pixels(overlap_rendered, baseline)) {
    std::cerr << "The overlapping display text stack did not render\n";
    return 1;
  }
  auto overlap_without_mask = overlap_state;
  auto unmasked_middle = stack_middle;
  unmasked_middle.insert(QStringLiteral("maskOpacity"), 1.0);
  overlap_without_mask.layers = QVariantList{stack_top, unmasked_middle, stack_bottom};
  const auto unmasked_rendered = render_output_image_with_backend(
      source, overlap_without_mask, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  if (unmasked_rendered.isNull() || same_image_pixels(overlap_rendered, unmasked_rendered)) {
    std::cerr << "The display overlay ignored the layer's own mask opacity\n";
    return 1;
  }

  auto full_canvas_mask_layer = stack_middle;
  full_canvas_mask_layer.insert(QStringLiteral("opacity"), 0.7);
  full_canvas_mask_layer.insert(QStringLiteral("maskOpacity"), 0.6);
  full_canvas_mask_layer.insert(QStringLiteral("maskData"), QByteArray(
      kWidth * kHeight, static_cast<char>(0x80)));
  auto full_canvas_mask_state = baseline_state;
  full_canvas_mask_state.layers = QVariantList{full_canvas_mask_layer};
  auto proportional_mask_layer = full_canvas_mask_layer;
  proportional_mask_layer.insert(QStringLiteral("maskData"), QByteArray(
      48 * 28, static_cast<char>(0x80)));
  proportional_mask_layer.insert(QStringLiteral("maskWidth"), 48);
  proportional_mask_layer.insert(QStringLiteral("maskHeight"), 28);
  auto proportional_mask_state = baseline_state;
  proportional_mask_state.layers = QVariantList{proportional_mask_layer};
  const auto full_canvas_mask_config =
      compile_color_pipeline_config(full_canvas_mask_state);
  const auto proportional_mask_config =
      compile_color_pipeline_config(proportional_mask_state);
  const auto full_canvas_mask_compiled = compile_state(
      full_canvas_mask_state, source.size(), full_canvas_mask_config);
  const auto proportional_mask_compiled = compile_state(
      proportional_mask_state, source.size(), proportional_mask_config);
  if (full_canvas_mask_compiled.display_referred_text_layers.size() != 1 ||
      proportional_mask_compiled.display_referred_text_layers.size() != 1 ||
      !full_canvas_mask_compiled.photo_layers.empty() ||
      !proportional_mask_compiled.photo_layers.empty()) {
    std::cerr << "Same-aspect full-canvas masks must remain eligible for display text overlays\n";
    return 1;
  }
  const auto full_canvas_mask_rendered = render_output_image_with_backend(
      source, full_canvas_mask_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  const auto proportional_mask_rendered = render_output_image_with_backend(
      source, proportional_mask_state, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  auto proportional_mask_full_opacity = proportional_mask_state;
  auto full_opacity_mask_layer = proportional_mask_layer;
  full_opacity_mask_layer.insert(QStringLiteral("maskOpacity"), 1.0);
  proportional_mask_full_opacity.layers = QVariantList{full_opacity_mask_layer};
  const auto proportional_mask_full_opacity_rendered = render_output_image_with_backend(
      source, proportional_mask_full_opacity, QColorSpace(QColorSpace::SRgb),
      QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
  if (full_canvas_mask_rendered.isNull() || proportional_mask_rendered.isNull() ||
      proportional_mask_full_opacity_rendered.isNull() ||
      !same_image_pixels(full_canvas_mask_rendered, proportional_mask_rendered) ||
      same_image_pixels(proportional_mask_rendered,
          proportional_mask_full_opacity_rendered)) {
    std::cerr << "A proportional full-canvas mask must preserve normalized edge coverage and mask opacity\n";
    return 1;
  }

  auto non_normal = plain_text_layer(QStringLiteral("non-normal-text"));
  non_normal.insert(QStringLiteral("blendMode"), QStringLiteral("screen"));
  auto non_normal_state = baseline_state;
  non_normal_state.layers = QVariantList{non_normal};
  if (!verify_unsupported_fallback(source, non_normal_state,
          QStringLiteral("non-normal-text"), QStringLiteral("Non-normal text")))
    return 1;

  auto material_text = plain_text_layer(QStringLiteral("material-text"));
  PhotoTextStyle advanced_style;
  advanced_style.material = QStringLiteral("soft_emboss");
  material_text.insert(QStringLiteral("textTypography"),
      encode_photo_text_style(advanced_style).toVariantMap());
  auto material_state = baseline_state;
  material_state.layers = QVariantList{material_text};
  const auto material_compiled = compile_state(
      material_state, source.size(), compile_color_pipeline_config(material_state));
  if (!material_compiled.display_referred_text_layers.empty() ||
      !has_layer_id(material_compiled.photo_layers, QStringLiteral("material-text"))) {
    std::cerr << "Text material effects must keep their layer in the ordinary compositor\n";
    return 1;
  }
  if (!verify_unsupported_fallback(source, material_state,
          QStringLiteral("material-text"), QStringLiteral("Material text")))
    return 1;

  auto scaled_mask_text = plain_text_layer(QStringLiteral("scaled-mask-text"));
  scaled_mask_text.insert(QStringLiteral("maskData"), QByteArray(
      48 * 32, static_cast<char>(0xff)));
  scaled_mask_text.insert(QStringLiteral("maskWidth"), 48);
  scaled_mask_text.insert(QStringLiteral("maskHeight"), 32);
  auto scaled_mask_state = baseline_state;
  scaled_mask_state.layers = QVariantList{scaled_mask_text};
  if (!verify_unsupported_fallback(source, scaled_mask_state,
          QStringLiteral("scaled-mask-text"), QStringLiteral("Mismatched-aspect text mask")))
    return 1;

  auto lower_text = plain_text_layer(QStringLiteral("interleaved-lower-text"));
  auto top_pixel = QVariantMap{
      {QStringLiteral("id"), QStringLiteral("interleaved-top-pixel")},
      {QStringLiteral("kind"), QStringLiteral("fill")},
      {QStringLiteral("fillColor"), QStringLiteral("#202020")},
      {QStringLiteral("canvasWidth"), kWidth},
      {QStringLiteral("canvasHeight"), kHeight},
  };
  auto interleaved_state = baseline_state;
  // Desktop recipes list layers top first. A non-text layer above the text
  // prevents moving the lower text out of its original compositing position.
  interleaved_state.layers = QVariantList{top_pixel, lower_text};
  const auto interleaved_compiled = compile_state(
      interleaved_state, source.size(), compile_color_pipeline_config(interleaved_state));
  if (!interleaved_compiled.display_referred_text_layers.empty() ||
      !has_layer_id(interleaved_compiled.photo_layers, QStringLiteral("interleaved-lower-text"))) {
    std::cerr << "Interleaved text must remain in its original working-scene position\n";
    return 1;
  }
  auto fill_only_state = baseline_state;
  fill_only_state.layers = QVariantList{top_pixel};
  const auto interleaved_rendered = render_output_image_with_backend(
      source, interleaved_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  const auto fill_only_rendered = render_output_image_with_backend(
      source, fill_only_state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  if (!same_image_pixels(interleaved_rendered, fill_only_rendered)) {
    std::cerr << "An opaque layer above interleaved text must retain legacy layer order\n";
    return 1;
  }

  const auto hdr_target = DisplayTarget{
      .target_name = "Rec.2020 PQ 1000 nits",
      .color_space = make_rec2020_color_space(),
      .eotf = DisplayEotf::Pq,
      .peak_luminance_nits = 1000.0,
      .black_luminance_nits = 0.0005,
      .reference_white_nits = 203.0,
      .surround_luminance_nits = 5.0,
      .surround = DisplaySurround::Dim,
      .mastering_mode = DisplayMasteringMode::HdrDisplayReferred,
  };
  const auto hdr = hdr_source();
  const auto* hdr_source_row =
      reinterpret_cast<const QRgbaFloat32*>(hdr.constScanLine(1));
  const auto overlay_coverage_is_zero_at = [&](int x, int y) {
    const auto index = static_cast<std::size_t>(y * kWidth + x);
    return std::all_of(
        compiled_overlay_layers.cbegin(), compiled_overlay_layers.cend(),
        [index](const PhotoLayer& layer) {
          return index < layer.pixels.size() &&
              layer.pixels[index].alpha <= 1.0e-7F;
        });
  };
  const auto source_highlight_is_eight =
      std::abs(hdr_source_row[1].red() - 8.0F) <= 1.0e-6F &&
      std::abs(hdr_source_row[1].green() - 8.0F) <= 1.0e-6F &&
      std::abs(hdr_source_row[1].blue() - 8.0F) <= 1.0e-6F;
  const auto source_background_is_point_one_eight =
      std::abs(hdr_source_row[10].red() - 0.18F) <= 1.0e-6F &&
      std::abs(hdr_source_row[10].green() - 0.18F) <= 1.0e-6F &&
      std::abs(hdr_source_row[10].blue() - 0.18F) <= 1.0e-6F;
  if (!source_highlight_is_eight || !source_background_is_point_one_eight ||
      !overlay_coverage_is_zero_at(1, 1) || !overlay_coverage_is_zero_at(10, 1)) {
    std::cerr << "HDR preservation probes must be the 8.0 and 0.18 source pixels outside text coverage\n";
    return 1;
  }
  auto hdr_config = compile_color_pipeline_config(overlay_state);
  hdr_config.export_display_target = hdr_target;
  hdr_config.export_output_profile = output_color_profile_from_name("Rec.2020");
  const auto hdr_compiled = compile_state(overlay_state, hdr.size(), hdr_config);
  if (!hdr_compiled.display_referred_text_layers.empty() ||
      !has_layer_id(hdr_compiled.photo_layers, QStringLiteral("cover-title"))) {
    std::cerr << "HDR text must stay in the working-scene stack until an HDR-safe overlay contract exists\n";
    return 1;
  }
  const auto hdr_plan = admitted_plan(
      QStringLiteral("display-text-hdr"), overlay_state, hdr.size(), DisplayRenderingTarget::HdrPq);
  const auto base_hdr_plan = admitted_plan(
      QStringLiteral("display-text-hdr-baseline"), baseline_state, hdr.size(), DisplayRenderingTarget::HdrPq);
  if (!hdr_plan.has_value() || !base_hdr_plan.has_value()) return 1;
  const auto hdr_target_space = linear_rec2020_color_space();
  const auto hdr_rendered = render_output_image_with_compiled_state(
      hdr, overlay_state, hdr_compiled, hdr_target_space, QImage::Format_RGBA32FPx4, *hdr_plan);
  const auto hdr_baseline_compiled = compile_state(
      baseline_state, hdr.size(), [&] {
        auto config = compile_color_pipeline_config(baseline_state);
        config.export_display_target = hdr_target;
        config.export_output_profile = output_color_profile_from_name("Rec.2020");
        return config;
      }());
  const auto hdr_baseline = render_output_image_with_compiled_state(
      hdr, baseline_state, hdr_baseline_compiled, hdr_target_space,
      QImage::Format_RGBA32FPx4, *base_hdr_plan);
  if (hdr_rendered.isNull() || hdr_baseline.isNull() ||
      hdr_rendered.format() != QImage::Format_RGBA32FPx4 ||
      hdr_baseline.format() != QImage::Format_RGBA32FPx4) {
    std::cerr << "Could not render HDR overlay fallback\n";
    return 1;
  }
  const auto* hdr_pixel = reinterpret_cast<const QRgbaFloat32*>(hdr_rendered.constScanLine(1));
  const auto* base_hdr_pixel = reinterpret_cast<const QRgbaFloat32*>(hdr_baseline.constScanLine(1));
  const auto linear_rec2020_relative_luminance = [](const QRgbaFloat32& pixel) {
    return 0.2627 * static_cast<double>(pixel.red()) +
        0.6780 * static_cast<double>(pixel.green()) +
        0.0593 * static_cast<double>(pixel.blue());
  };
  const auto highlight_relative_luminance =
      linear_rec2020_relative_luminance(hdr_pixel[1]);
  const auto baseline_highlight_relative_luminance =
      linear_rec2020_relative_luminance(base_hdr_pixel[1]);
  const auto background_relative_luminance =
      linear_rec2020_relative_luminance(base_hdr_pixel[10]);
  const auto highlight_luminance_nits =
      highlight_relative_luminance * hdr_target.reference_white_nits;
  const auto baseline_highlight_luminance_nits =
      baseline_highlight_relative_luminance * hdr_target.reference_white_nits;
  const auto background_luminance_nits =
      background_relative_luminance * hdr_target.reference_white_nits;
  const auto exact_highlight_preservation =
      std::abs(hdr_pixel[1].red() - base_hdr_pixel[1].red()) <= 1.0e-6F &&
      std::abs(hdr_pixel[1].green() - base_hdr_pixel[1].green()) <= 1.0e-6F &&
      std::abs(hdr_pixel[1].blue() - base_hdr_pixel[1].blue()) <= 1.0e-6F;
  const auto valid_hdr_target_metadata =
      hdr_target.eotf == DisplayEotf::Pq &&
      hdr_target.mastering_mode == DisplayMasteringMode::HdrDisplayReferred &&
      hdr_target.reference_white_nits > 0.0 &&
      hdr_target.peak_luminance_nits > hdr_target.reference_white_nits;
  const auto output_within_hdr_peak =
      std::isfinite(highlight_luminance_nits) &&
      highlight_luminance_nits <= hdr_target.peak_luminance_nits + 1.0e-3;
  const auto output_preserves_highlight_separation =
      std::isfinite(background_luminance_nits) &&
      highlight_luminance_nits > background_luminance_nits + 1.0;
  std::cout << "HDR linear Rec.2020 relative RGB at highlight [1,1]: rendered=("
            << hdr_pixel[1].red() << ',' << hdr_pixel[1].green() << ','
            << hdr_pixel[1].blue() << ") baseline=(" << base_hdr_pixel[1].red()
            << ',' << base_hdr_pixel[1].green() << ',' << base_hdr_pixel[1].blue()
            << "), highlight=" << highlight_luminance_nits << " nits, baseline="
            << baseline_highlight_luminance_nits << " nits, nearby background="
            << background_luminance_nits << " nits; reference white="
            << hdr_target.reference_white_nits << " nits, peak="
            << hdr_target.peak_luminance_nits << " nits\n";
  if (!exact_highlight_preservation) {
    std::cerr << "HDR fallback changed the original output highlight RGB values\n";
    return 1;
  }
  if (!valid_hdr_target_metadata || !output_within_hdr_peak ||
      !output_preserves_highlight_separation) {
    std::cerr << "HDR output lost highlight separation or exceeded its configured target metadata\n";
    return 1;
  }

  QString preview_error;
  if (!verify_native_preview_suffix_parity(
          source, baseline_state, overlay_state,
          compiled_overlay_layers.front(), &preview_error)) {
    std::cerr << preview_error.toStdString() << '\n';
    return 1;
  }

  bool monitor_icc_skipped = false;
  QString monitor_icc_error;
  if (!verify_system_monitor_icc_preview(
          source, QColor(QStringLiteral("#f2ebdc")),
          &monitor_icc_skipped, &monitor_icc_error)) {
    if (monitor_icc_skipped) {
      std::cerr << "system monitor ICC integration skipped: "
                << monitor_icc_error.toStdString() << '\n';
      return 77;
    }
    std::cerr << monitor_icc_error.toStdString() << '\n';
    return 1;
  }

  QString file_effects_error;
  if (!verify_task_owned_photo_fragment_file_effects(&file_effects_error)) {
    std::cerr << file_effects_error.toStdString() << '\n';
    return 1;
  }

  std::cout << "display text overlay regression: SDR authored colors/alpha, cache identity, "
               "overlap and proportional-mask parity, exact outside-ink pixels, legacy fallback, "
               "HDR highlight preservation, native-surface-eligible preview/export parity, and "
               "actual system-monitor ICC preview/text/mask parity plus task-owned Photo Fragment file-effect guards passed\n";
  return 0;
}
