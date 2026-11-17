#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include "render_executor_service.h"
#include "working_image.h"

#include "photo_editor/io/openexr_io.h"
#include "photo_editor/pipeline/color_space.h"

#include <QBuffer>
#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QUuid>
#include <QtGui/qrgbafloat.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}  // namespace photo_editor::desktop

namespace {

constexpr int kSourceWidth = 12;
constexpr int kSourceHeight = 8;
constexpr int kPatchX = 4;
constexpr int kPatchY = 2;
constexpr int kPatchWidth = 4;
constexpr int kPatchHeight = 2;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class TestImageSink final : public EditorPreviewImageSink {
 public:
  void setImage(const QString& key, const QImage& image) override {
    if (image.isNull()) images_.remove(key);
    else images_.insert(key, image);
  }

  void clearImage(const QString& key) override { images_.remove(key); }

  QImage image(const QString& key) const override { return images_.value(key); }

 private:
  QHash<QString, QImage> images_;
};

template <typename Predicate>
void wait_until(Predicate done, const char* message, int timeout_ms = 90000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(done(), message);
}

void settle(EditorPreviewController& editor, const char* message) {
  wait_until([&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() &&
        !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
        !editor.selectionWorkBusy() && !editor.photoExportBusy();
  }, message);
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok() && parsed.scope == AdjustmentPatchScope::AssetBound,
          "The Heal edit must remain an asset-bound canonical recipe.");
  return *parsed.state;
}

QVariantMap heal_mask(const EditRecipeState& state) {
  for (const auto& value : state.mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("hdr-heal-mask")) {
      return mask;
    }
  }
  return {};
}

QImage provider_preview(const TestImageSink& sink, const QString& source) {
  const QUrl url(source);
  if (url.isLocalFile()) return QImage(url.toLocalFile());
  if (url.scheme().isEmpty() && QFileInfo(source).isFile()) return QImage(source);
  if (url.scheme() != QStringLiteral("image") ||
      url.host() != QStringLiteral("editor-preview")) {
    return {};
  }
  auto key = url.path();
  if (key.startsWith(QLatin1Char('/'))) key.remove(0, 1);
  return sink.image(key);
}

std::vector<float> make_hdr_samples() {
  std::vector<float> samples;
  samples.reserve(static_cast<std::size_t>(kSourceWidth * kSourceHeight * 4));
  for (int y = 0; y < kSourceHeight; ++y) {
    for (int x = 0; x < kSourceWidth; ++x) {
      const auto alpha = x == 5 && y == 2 ? 0.0F
          : x == 6 && y == 2 ? 0.25F
          : x == 5 && y == 3 ? 0.5F
          : x == 6 && y == 3 ? 1.0F
          : (x % 3 == 0 ? 0.0F : 1.0F);
      samples.push_back(2.0F + static_cast<float>(x % 3) * 0.3F +
                        static_cast<float>(y) * 0.025F);
      samples.push_back(-0.9F + static_cast<float>(x) * 0.018F +
                        static_cast<float>(y) * 0.012F);
      samples.push_back(0.2F + static_cast<float>((x + y) % 4) * 0.08F);
      samples.push_back(alpha);
    }
  }
  return samples;
}

void write_hdr_source(const QString& path, const std::vector<float>& samples) {
  std::string error;
  const auto wrote = photo_editor::io::write_openexr_rgba(
      path.toStdString(), samples,
      photo_editor::io::OpenExrWriteOptions{
          .width = kSourceWidth,
          .height = kSourceHeight,
          .row_stride_floats = static_cast<std::size_t>(kSourceWidth) * 4U,
          .half_float = false,
          .include_alpha = true,
          .color_space = photo_editor::pipeline::make_linear_srgb_color_space()},
      &error);
  require(wrote, "The authored full-float EXR source must be writable.");
}

QByteArray make_patch_png() {
  QImage patch(kPatchWidth, kPatchHeight, QImage::Format_ARGB32);
  patch.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const QColor colors[] = {
      QColor(55, 75, 105), QColor(220, 185, 135),
      QColor(110, 205, 170), QColor(190, 80, 150)};
  for (int y = 0; y < kPatchHeight; ++y) {
    for (int x = 0; x < kPatchWidth; ++x) {
      patch.setPixelColor(x, y, colors[(x + y) % 4]);
    }
  }
  QByteArray bytes;
  QBuffer buffer(&bytes);
  require(buffer.open(QIODevice::WriteOnly) && patch.save(&buffer, "PNG"),
          "The authored Heal patch must serialize as an ordinary PNG payload.");
  return bytes;
}

QByteArray make_heal_matte() {
  QByteArray matte(kSourceWidth * kSourceHeight, '\0');
  for (const auto [x, y] : {std::pair{kPatchX + 1, kPatchY},
                            std::pair{kPatchX + 2, kPatchY},
                            std::pair{kPatchX + 1, kPatchY + 1},
                            std::pair{kPatchX + 2, kPatchY + 1}}) {
    matte[y * kSourceWidth + x] = static_cast<char>(255);
  }
  return matte;
}

EditRecipeState make_initial_recipe() {
  EditRecipeState state;
  state.working_color_profile = QStringLiteral("Linear sRGB");
  state.preview_color_space = QStringLiteral("sRGB");
  state.output_color_space = QStringLiteral("sRGB");
  state.active_mask_index = 0;
  state.mask_stack = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("hdr-heal-mask")},
      {QStringLiteral("name"), QStringLiteral("HDR Heal")},
      {QStringLiteral("kind"), QStringLiteral("brush")},
      {QStringLiteral("toolRole"), QStringLiteral("retouch")},
      {QStringLiteral("retouchMode"), QStringLiteral("heal")},
      {QStringLiteral("operation"), QStringLiteral("add")},
      {QStringLiteral("coordinateSpace"), QStringLiteral("source")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("committed"), true},
      {QStringLiteral("removeStrength"), 100.0},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("density"), 1.0},
      {QStringLiteral("feather"), 0.0},
      {QStringLiteral("healTextureBlend"), 0.5},
      {QStringLiteral("healColorBlend"), 0.25},
      {QStringLiteral("segmentationWidth"), kSourceWidth},
      {QStringLiteral("segmentationHeight"), kSourceHeight},
      {QStringLiteral("segmentationData"), make_heal_matte()},
      {QStringLiteral("inpaintPatchData"), make_patch_png()},
      {QStringLiteral("inpaintPatchX"), kPatchX},
      {QStringLiteral("inpaintPatchY"), kPatchY},
      {QStringLiteral("inpaintPatchWidth"), kPatchWidth},
      {QStringLiteral("inpaintPatchHeight"), kPatchHeight},
      {QStringLiteral("inpaintPatchColorSpace"), QStringLiteral("sRGB")},
      {QStringLiteral("inpaintBlendAmount"), 1.0},
      {QStringLiteral("inpaintColorMatch"), 0.0},
      {QStringLiteral("inpaintSeamFeather"), 0.0}}};
  return state;
}

QImage scene_linear_render(
    const QImage& source,
    const EditRecipeState& state,
    photo_editor::pipeline::RemovalQuality quality =
        photo_editor::pipeline::RemovalQuality::Final,
    RenderMaskOptions options = {}) {
  const auto result = render_output_image_with_backend(
      source, state, working_color_space(state.working_color_profile),
      QImage::Format_RGBA32FPx4, RenderBackendPreference::CpuOnly, nullptr,
      nullptr, options, quality, RenderOutputPolicy::SceneLinearWorking);
  require(!result.isNull() && result.format() == QImage::Format_RGBA32FPx4 &&
              result.colorSpace() == working_color_space(state.working_color_profile),
          "The production renderer must return tagged float working pixels before output mapping.");
  return result;
}

QImage scene_linear_render_format(
    const QImage& source,
    const EditRecipeState& state,
    QImage::Format output_format,
    photo_editor::pipeline::RemovalQuality quality =
        photo_editor::pipeline::RemovalQuality::Final,
    RenderMaskOptions options = {}) {
  auto telemetry = RenderBackendTelemetry{};
  const auto result = render_output_image_with_backend(
      source, state, working_color_space(state.working_color_profile),
      output_format, RenderBackendPreference::CpuOnly, nullptr,
      &telemetry, options, quality, RenderOutputPolicy::SceneLinearWorking);
  require(!result.isNull() && result.format() == output_format &&
              result.colorSpace() == working_color_space(state.working_color_profile),
          "The production renderer must return tagged float working pixels before output mapping.");
  return result;
}

QRgbaFloat32 float_pixel(const QImage& image, int x, int y) {
  return reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y))[x];
}

struct FloatSample final {
  float red = 0.0F;
  float green = 0.0F;
  float blue = 0.0F;
  float alpha = 0.0F;
};

FloatSample float_sample(const QImage& image, int x, int y) {
  if (image.format() == QImage::Format_RGBA16FPx4 ||
      image.format() == QImage::Format_RGBA16FPx4_Premultiplied) {
    const auto pixel = reinterpret_cast<const QRgbaFloat16*>(image.constScanLine(y))[x];
    return {static_cast<float>(pixel.r), static_cast<float>(pixel.g),
            static_cast<float>(pixel.b), static_cast<float>(pixel.a)};
  }
  const auto pixel = reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y))[x];
  return {pixel.red(), pixel.green(), pixel.blue(), pixel.alpha()};
}

bool same_float_pixels(const QImage& left, const QImage& right,
                       float tolerance = 2.0e-5F) {
  if (left.isNull() || right.isNull() || left.size() != right.size() ||
      left.format() != QImage::Format_RGBA32FPx4 ||
      right.format() != QImage::Format_RGBA32FPx4) {
    return false;
  }
  for (int y = 0; y < left.height(); ++y) {
    for (int x = 0; x < left.width(); ++x) {
      const auto a = float_pixel(left, x, y);
      const auto b = float_pixel(right, x, y);
      if (std::abs(a.red() - b.red()) > tolerance ||
          std::abs(a.green() - b.green()) > tolerance ||
          std::abs(a.blue() - b.blue()) > tolerance ||
          std::abs(a.alpha() - b.alpha()) > tolerance) {
        return false;
      }
    }
  }
  return true;
}

bool same_float_pixel(const QImage& left, const QImage& right, int x, int y,
                      float tolerance = 2.0e-5F) {
  const auto a = float_pixel(left, x, y);
  const auto b = float_pixel(right, x, y);
  return std::abs(a.red() - b.red()) <= tolerance &&
      std::abs(a.green() - b.green()) <= tolerance &&
      std::abs(a.blue() - b.blue()) <= tolerance &&
      std::abs(a.alpha() - b.alpha()) <= tolerance;
}

QImage display_render(const QImage& source, const EditRecipeState& state) {
  const auto result = render_output_image_with_backend(
      source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!result.isNull(), "The display-referred Heal render must succeed.");
  return result;
}

int max_display_channel_delta(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return -1;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  auto maximum = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      const auto* ap = a.constScanLine(y) + x * 4;
      const auto* bp = b.constScanLine(y) + x * 4;
      for (int channel = 0; channel < 4; ++channel) {
        maximum = std::max(maximum,
            std::abs(static_cast<int>(static_cast<unsigned char>(ap[channel])) -
                     static_cast<int>(static_cast<unsigned char>(bp[channel]))));
      }
    }
  }
  return maximum;
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset,
                                         bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  wait_until([&finished]() { return finished; },
             "Heal undo/redo must emit its history completion signal.");
  QObject::disconnect(connection);
  require(succeeded, "Heal undo/redo must restore the asset successfully.");
  settle(editor, "The restored HDR document render must settle.");
}

void verify_cold_warm_disk_preview_cache(
    const QString& source_path,
    const QString& runtime_path,
    const EditRecipeState& state,
    const QString& asset_id) {
  const auto disk_source_path = QDir(runtime_path).filePath(
      QStringLiteral("hdr-heal-disk-preview-source.exr"));
  require(QFile::copy(source_path, disk_source_path),
          "The disk-cache check must use a private generated source path.");
  RenderRequest request;
  request.source_asset_id = asset_id + QStringLiteral("-disk-preview");
  request.file_path = disk_source_path;
  request.state = state;
  request.full_resolution = true;
  request.use_in_memory_preview = false;
  request.require_cpu_preview_image = true;
  request.require_cpu_display_image = true;
  request.tile_size = 512;
  request.render_mode = QStringLiteral("max_preview");
  request.recipe_cache_identity = adjustment_patch_identity_json(state);
  request.adjusted_recipe_cache_identity = request.recipe_cache_identity;

  // Clear process-local execution caches so the warm render must come from
  // the isolated disk PNG cache, not a resident render stage.
  preview_render_executor().clear_cache();
  export_render_executor().clear_cache();
  const auto cold = render_preview_request_for_test(request);
  const auto cold_path = QUrl(cold.preview_source).toLocalFile();
  require(cold.success && !cold.disk_cache_hit && !cold.preview_image.isNull() &&
              QFileInfo(cold_path).isFile(),
          "The first full-resolution preview request must create a cold disk cache entry.");

  preview_render_executor().clear_cache();
  export_render_executor().clear_cache();
  const auto warm = render_preview_request_for_test(request);
  require(warm.success && warm.disk_cache_hit && warm.preview_source == cold.preview_source &&
              !warm.preview_image.isNull() &&
              warm.preview_image == cold.preview_image &&
              warm.preview_image.colorSpace() == cold.preview_image.colorSpace(),
          "A warm disk preview must report a hit and reproduce identical pixels after executor caches are cleared.");
}

void write_engine_float_exr_roundtrip(const QString& path, const QImage& image) {
  const auto* rgba = image.constBits();
  require(rgba != nullptr, "The flattened float render must expose pixel storage.");
  std::vector<float> samples;
  samples.reserve(static_cast<std::size_t>(image.width() * image.height() * 4));
  for (int y = 0; y < image.height(); ++y) {
    const auto* row = reinterpret_cast<const QRgbaFloat32*>(image.constScanLine(y));
    for (int x = 0; x < image.width(); ++x) {
      samples.push_back(row[x].red());
      samples.push_back(row[x].green());
      samples.push_back(row[x].blue());
      samples.push_back(row[x].alpha());
    }
  }
  std::string error;
  const auto wrote = photo_editor::io::write_openexr_rgba(
      path.toStdString(), samples,
      photo_editor::io::OpenExrWriteOptions{
          .width = image.width(),
          .height = image.height(),
          .row_stride_floats = static_cast<std::size_t>(image.width()) * 4U,
          .half_float = false,
          .include_alpha = true,
          .color_space = photo_editor::pipeline::make_linear_srgb_color_space()},
      &error);
  require(wrote, "The engine OpenEXR writer must accept flattened float Heal pixels.");
  const auto roundtrip = decode_photo_layer_source_image(path);
  require(!roundtrip.isNull() && roundtrip.size() == image.size() &&
              roundtrip.format() == QImage::Format_RGBA32FPx4,
          "The engine OpenEXR writer round trip must retain float pixels and dimensions.");
  require(same_float_pixels(image, roundtrip),
          "The engine OpenEXR writer round trip must retain HDR, signed RGB, and alpha.");
}

void test_asset_backed_hdr_heal(const QString& runtime_path) {
  if (!photo_editor::io::openexr_backend_available()) {
    std::cout << "SKIP: HDR Heal workflow; OpenEXR backend unavailable\n";
    return;
  }

  const auto source_path = QDir(runtime_path).filePath(QStringLiteral("hdr-heal-source.exr"));
  const auto source_samples = make_hdr_samples();
  write_hdr_source(source_path, source_samples);
  const auto source = decode_photo_layer_source_image(source_path);
  require(!source.isNull() && source.size() == QSize(kSourceWidth, kSourceHeight) &&
              source.format() == QImage::Format_RGBA32FPx4,
          "The source fixture must decode through the normal EXR source path as float pixels.");
  const auto source_highlight = float_pixel(source, 5, 2);
  const auto source_signed = float_pixel(source, 5, 2);
  require(source_highlight.red() > 2.0F && source_signed.green() < -0.7F &&
              float_pixel(source, 5, 2).alpha() == 0.0F &&
              float_pixel(source, 6, 2).alpha() == 0.25F &&
              float_pixel(source, 5, 3).alpha() == 0.5F &&
              float_pixel(source, 6, 3).alpha() == 1.0F,
          "The authored EXR must carry above-white, signed, zero, fractional, and opaque samples.");

  auto initial_state = make_initial_recipe();
  auto initial_payload = serialize_adjustment_patch_json(
      initial_state, all_adjustment_groups(), AdjustmentPatchScope::AssetBound);
  require(!initial_payload.isEmpty(),
          "The generated Heal recipe must serialize as an asset-bound adjustment patch.");

  TestImageSink sink;
  EditorPreviewController editor;
  editor.setImageProvider(&sink);
  const auto asset_id = QStringLiteral("hdr-retouch-workflow-%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  editor.loadAsset(asset_id, source_path, {}, {}, {}, {}, initial_payload, true);
  settle(editor, "The asset-backed HDR Heal recipe must render in the editor.");
  const auto loaded_mask_stack = editor.maskStack();
  auto heal_index = -1;
  for (int index = 0; index < loaded_mask_stack.size(); ++index) {
    if (loaded_mask_stack.at(index).toMap().value(QStringLiteral("id")).toString() ==
        QStringLiteral("hdr-heal-mask")) {
      heal_index = index;
      break;
    }
  }
  require(heal_index >= 0,
          "The loaded controller mask stack must contain the authored HDR Heal mask ID.");
  editor.setActiveMaskIndex(heal_index);
  require(editor.activeMaskIndex() == heal_index &&
              editor.maskStack().at(editor.activeMaskIndex()).toMap()
                  .value(QStringLiteral("id")).toString() == QStringLiteral("hdr-heal-mask"),
          "The test must select the actual authored Heal mask before editing its controls.");
  const auto initial_recipe = recipe(editor);
  const auto loaded_mask = heal_mask(initial_recipe);
  require(initial_recipe.working_color_profile == QStringLiteral("Linear sRGB") &&
              !loaded_mask.isEmpty() &&
              loaded_mask.value(QStringLiteral("healTextureBlend")).toDouble() == 0.5 &&
              loaded_mask.value(QStringLiteral("healColorBlend")).toDouble() == 0.25 &&
              loaded_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().size() > 0,
          "The controller must load the authored recipe, separate blend controls, patch, and matte.");

  auto baseline_state = initial_recipe;
  baseline_state.mask_stack.clear();
  const auto untouched = scene_linear_render(source, baseline_state);
  const auto before_texture_half = scene_linear_render(source, initial_recipe);

  editor.setActiveMaskHealColorBlend(0.5);
  settle(editor, "The Heal color blend edit must finish its preview render.");
  const auto after_recipe = recipe(editor);
  const auto after_mask = heal_mask(after_recipe);
  const auto blends_reached_half =
      after_mask.value(QStringLiteral("healTextureBlend")).toDouble() == 0.5 &&
      after_mask.value(QStringLiteral("healColorBlend")).toDouble() == 0.5;
  if (!blends_reached_half) {
    const auto active_index = editor.activeMaskIndex();
    const auto active_mask = active_index >= 0 && active_index < editor.maskStack().size()
        ? editor.maskStack().at(active_index).toMap() : QVariantMap{};
    std::cerr << "HDR Heal blend diagnostic: status=" << editor.status().toStdString()
              << " activeMaskIndex=" << active_index
              << " activeMaskId="
              << active_mask.value(QStringLiteral("id")).toString().toStdString()
              << " activeRetouchMode="
              << active_mask.value(QStringLiteral("retouchMode")).toString().toStdString()
              << " texture="
              << after_mask.value(QStringLiteral("healTextureBlend")).toDouble()
              << " color="
              << after_mask.value(QStringLiteral("healColorBlend")).toDouble()
              << '\n';
  }
  require(blends_reached_half,
          "The authored texture and color blend controls must both reach the 0.5 working value.");

  const RenderMaskOptions full_options{
      .rebuild_editable_masks_at_output_resolution = true,
      .source_geometry = DesktopSourceGeometry{source.size(), source.rect()}};
  export_render_executor().clear_cache();
  const auto final_working = scene_linear_render(
      source, after_recipe, photo_editor::pipeline::RemovalQuality::Final, full_options);
  struct SelectedAlphaSample final {
    int x;
    int y;
    float alpha;
  };
  const SelectedAlphaSample selected_alpha_samples[] = {
      {5, 2, 0.0F}, {6, 2, 0.25F}, {5, 3, 0.5F}, {6, 3, 1.0F}};
  for (const auto formats : {
           std::pair{QImage::Format_RGBA16FPx4,
                     QImage::Format_RGBA16FPx4_Premultiplied},
           std::pair{QImage::Format_RGBA32FPx4,
                     QImage::Format_RGBA32FPx4_Premultiplied}}) {
    const auto straight = formats.first == QImage::Format_RGBA32FPx4
        ? final_working
        : scene_linear_render_format(
              source, after_recipe, formats.first,
              photo_editor::pipeline::RemovalQuality::Final, full_options);
    const auto premultiplied = scene_linear_render_format(
        source, after_recipe, formats.second,
        photo_editor::pipeline::RemovalQuality::Final, full_options);
    const auto tolerance = formats.first == QImage::Format_RGBA16FPx4
        ? 0.003F : 2.0e-5F;
    require(straight.colorSpace() == working_color_space(after_recipe.working_color_profile) &&
                premultiplied.colorSpace() == working_color_space(after_recipe.working_color_profile),
            "All straight and premultiplied float outputs must retain the working color-space tag.");
    for (const auto& sample : selected_alpha_samples) {
      const auto x = sample.x;
      const auto y = sample.y;
      const auto expected_alpha = sample.alpha;
      const auto straight_pixel = float_sample(straight, x, y);
      const auto premultiplied_pixel = float_sample(premultiplied, x, y);
      require(std::abs(straight_pixel.alpha - expected_alpha) <= tolerance &&
                  std::abs(premultiplied_pixel.alpha - expected_alpha) <= tolerance,
              "Straight and premultiplied float formats must retain alpha 0, 0.25, 0.5, and 1.");
      if (expected_alpha == 0.0F) {
        require(std::abs(premultiplied_pixel.red) <= tolerance &&
                    std::abs(premultiplied_pixel.green) <= tolerance &&
                    std::abs(premultiplied_pixel.blue) <= tolerance,
                "Premultiplied float output must zero RGB at zero alpha.");
      } else {
        require(std::abs(premultiplied_pixel.red - straight_pixel.red * expected_alpha) <= tolerance &&
                    std::abs(premultiplied_pixel.green - straight_pixel.green * expected_alpha) <= tolerance &&
                    std::abs(premultiplied_pixel.blue - straight_pixel.blue * expected_alpha) <= tolerance,
                "Premultiplied float output RGB must equal straight RGB multiplied by alpha.");
      }
    }
    const auto straight_hdr = float_sample(straight, 6, 2);
    const auto premultiplied_hdr = float_sample(premultiplied, 6, 2);
    require(straight_hdr.alpha == 0.25F && straight_hdr.red > 1.0F &&
                straight_hdr.green < 0.0F &&
                std::abs(premultiplied_hdr.alpha - 0.25F) <= tolerance &&
                premultiplied_hdr.red / premultiplied_hdr.alpha > 1.0F &&
                premultiplied_hdr.green / premultiplied_hdr.alpha < 0.0F,
            "All float formats must retain signed RGB and above-white radiance at partial alpha.");
  }
  const auto interactive_working = scene_linear_render(
      source, after_recipe, photo_editor::pipeline::RemovalQuality::Interactive, full_options);
  const auto final_repeat = scene_linear_render(
      source, after_recipe, photo_editor::pipeline::RemovalQuality::Final, full_options);
  require(same_float_pixels(final_working, final_repeat),
          "Repeated uncached Final working renders must be pixel-identical.");

  const auto repaired_hdr = float_pixel(final_working, 6, 2);
  const auto untouched_hdr = float_pixel(untouched, 6, 2);
  const auto source_hdr = float_pixel(source, 6, 2);
  const auto source_alpha_zero = float_pixel(source, 5, 2).alpha();
  const auto source_alpha_quarter = float_pixel(source, 6, 2).alpha();
  const auto source_alpha_half = float_pixel(source, 5, 3).alpha();
  const auto source_alpha_full = float_pixel(source, 6, 3).alpha();
  const auto repaired_alpha_zero = float_pixel(final_working, 5, 2).alpha();
  const auto repaired_alpha_quarter = float_pixel(final_working, 6, 2).alpha();
  const auto repaired_alpha_half = float_pixel(final_working, 5, 3).alpha();
  const auto repaired_alpha_full = float_pixel(final_working, 6, 3).alpha();
  const auto preserves_hdr_domain = repaired_hdr.red() > 1.0F &&
      repaired_hdr.green() < 0.0F;
  const auto preserves_alpha = repaired_alpha_zero == 0.0F &&
      repaired_alpha_quarter == 0.25F && repaired_alpha_half == 0.5F &&
      repaired_alpha_full == 1.0F;
  if (!preserves_hdr_domain || !preserves_alpha) {
    const auto print_pixel = [](const char* label, const QRgbaFloat32& pixel) {
      std::cerr << label << "=(" << pixel.red() << ',' << pixel.green() << ','
                << pixel.blue() << ',' << pixel.alpha() << ") ";
    };
    std::cerr << "HDR Heal working-pixel diagnostic: ";
    print_pixel("source@6,2", source_hdr);
    print_pixel("untouched@6,2", untouched_hdr);
    print_pixel("repaired@6,2", repaired_hdr);
    std::cerr << "alpha source/final at selected coordinates: "
              << "(5,2)=" << source_alpha_zero << '/' << repaired_alpha_zero << ' '
              << "(6,2)=" << source_alpha_quarter << '/' << repaired_alpha_quarter << ' '
              << "(5,3)=" << source_alpha_half << '/' << repaired_alpha_half << ' '
              << "(6,3)=" << source_alpha_full << '/' << repaired_alpha_full << '\n';
  }
  require(preserves_hdr_domain && preserves_alpha,
          "Final Heal must retain headroom, signed RGB, and exact source alpha before output mapping.");
  const auto interactive_hdr = float_pixel(interactive_working, 6, 2);
  require(interactive_hdr.red() > 1.0F && interactive_hdr.green() < 0.0F &&
              interactive_hdr.alpha() == 0.25F,
          "Interactive Heal working pixels must retain HDR and signed channels before output mapping.");

  for (int y = 0; y < kSourceHeight; ++y) {
    for (int x = 0; x < kSourceWidth; ++x) {
      const auto selected = x >= kPatchX + 1 && x <= kPatchX + 2 &&
          y >= kPatchY && y <= kPatchY + 1;
      if (!selected) {
        require(same_float_pixel(final_working, untouched, x, y),
                "The Heal matte must leave every unselected HDR source pixel untouched.");
      }
      require(std::abs(float_pixel(final_working, x, y).alpha() -
                       float_pixel(untouched, x, y).alpha()) <= 1.0e-6F,
              "Heal must preserve alpha at selected and unselected pixels.");
    }
  }
  require(!same_float_pixel(final_working, before_texture_half, 6, 2),
          "Changing Heal color blend from 0.25 to 0.5 must change its scene-linear target pixel.");

  auto no_texture_state = after_recipe;
  auto no_texture_mask = heal_mask(no_texture_state);
  no_texture_mask.insert(QStringLiteral("healTextureBlend"), 0.0);
  for (auto& value : no_texture_state.mask_stack) {
    if (value.toMap().value(QStringLiteral("id")).toString() ==
        QStringLiteral("hdr-heal-mask")) {
      value = no_texture_mask;
      break;
    }
  }
  const auto no_texture_working = scene_linear_render(
      source, no_texture_state, photo_editor::pipeline::RemovalQuality::Final, full_options);
  require(!same_float_pixel(final_working, no_texture_working, 6, 2),
          "The 0.5 texture blend must contribute patch texture in scene-linear output.");

  verify_cold_warm_disk_preview_cache(source_path, runtime_path, after_recipe, asset_id);

  const QRect crop(2, 1, 8, 6);
  const auto cropped_source = source.copy(crop);
  const RenderMaskOptions crop_options{
      .rebuild_editable_masks_at_output_resolution = true,
      .source_geometry = DesktopSourceGeometry{source.size(), crop},
      .full_source = source};
  const auto cropped_working = scene_linear_render(
      cropped_source, after_recipe, photo_editor::pipeline::RemovalQuality::Final,
      crop_options);
  require(same_float_pixels(cropped_working, final_working.copy(crop)),
          "A physically cropped Heal render must equal the matching full-source HDR pixels.");

  const auto display_expected = display_render(source, after_recipe);
  const auto export_path = QDir(runtime_path).filePath(QStringLiteral("hdr-heal-display.png"));
  require(editor.startFlattenedPhotoExport(export_path),
          "The controller must start an ordinary display-referred flattened PNG export.");
  settle(editor, "The flattened PNG export must finish.");
  require(editor.photoExportError().isEmpty(),
          "The flattened PNG export must complete without an error.");
  const QImage exported_png(export_path);
  require(!exported_png.isNull() && exported_png.size() == display_expected.size() &&
              max_display_channel_delta(exported_png, display_expected) <= 2,
          "Display PNG export parity must be checked separately from float HDR proof.");

  const auto engine_exr_path = QDir(runtime_path).filePath(QStringLiteral("hdr-heal-engine-roundtrip.exr"));
  write_engine_float_exr_roundtrip(engine_exr_path, final_working);

  const auto saved_payload = canonical_asset_bound_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(!saved_payload.isEmpty(), "The edited asset-bound recipe must canonicalize for reopen.");
  const auto saved_parse = parse_adjustment_patch_json(saved_payload);
  require(saved_parse.ok() && saved_parse.scope == AdjustmentPatchScope::AssetBound &&
              saved_parse.state->mask_stack == after_recipe.mask_stack,
          "The canonical asset-bound recipe must preserve the accepted Heal patch and matte.");

  restore_history(editor, false);
  require(recipe(editor) == initial_recipe,
          "Undo must restore the exact prior HDR Heal recipe and 0.25 color blend.");
  restore_history(editor, true);
  require(recipe(editor) == after_recipe,
          "Redo must restore the accepted HDR Heal recipe and 0.5 color blend.");

  TestImageSink reopened_sink;
  EditorPreviewController reopened;
  reopened.setImageProvider(&reopened_sink);
  reopened.loadAsset(asset_id, source_path, {}, {}, {}, {}, saved_payload, true);
  settle(reopened, "The canonical asset-bound HDR Heal recipe must reopen in a fresh controller.");
  const auto reopened_recipe = recipe(reopened);
  require(reopened_recipe == after_recipe,
          "Canonical reopen must preserve the exact Heal recipe state.");
  const auto reopened_working = scene_linear_render(
      source, reopened_recipe, photo_editor::pipeline::RemovalQuality::Final, full_options);
  require(same_float_pixels(reopened_working, final_working),
          "The reopened asset-bound recipe must reproduce identical flattened float pixels.");

  const auto published_preview_source = reopened.displayPreviewSource().isEmpty()
      ? reopened.previewSource() : reopened.displayPreviewSource();
  const auto cached_preview = provider_preview(reopened_sink, published_preview_source);
  if (cached_preview.isNull()) {
    std::cerr << "Cached preview diagnostic: displayPreviewSource="
              << reopened.displayPreviewSource().toStdString()
              << " previewSource="
              << reopened.previewSource().toStdString()
              << " status=" << reopened.status().toStdString()
              << " fullRenderRunning=" << reopened.fullRenderRunning()
              << " sourceSize=" << reopened.sourceImageWidth() << 'x'
              << reopened.sourceImageHeight()
              << " previewFailure=" << reopened.previewFailureStage().toStdString()
              << '/' << reopened.previewFailureDetail().toStdString() << '\n';
  }
  require(!cached_preview.isNull(),
          "The reopened editor must publish a cached display preview for parity checks.");
  auto mapped_expected = display_expected;
  if (cached_preview.size() != mapped_expected.size()) {
    mapped_expected = mapped_expected.scaled(
        cached_preview.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  }
  require(max_display_channel_delta(cached_preview, mapped_expected) <= 2,
          "The controller preview cache and direct uncached display render must agree.");

  std::cout << "HDR Heal workflow passed: float EXR source, signed/headroom scene-linear "
               "preview and Final, 0.5 separated blends, matte scope, crop parity, "
               "asset-bound reopen, undo/redo, engine EXR writer roundtrip, and PNG parity\n";
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          QDir(runtime.path()).filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          QDir(runtime.path()).filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  qputenv("QT_QPA_PLATFORM", "offscreen");
  qputenv("QT_QUICK_BACKEND", "software");
  qputenv("QSG_RHI_BACKEND", "software");
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyHdrRetouchWorkflowTests"));
  QCoreApplication::setApplicationName(QStringLiteral("HdrRetouch-%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication application(argc, argv);
  struct DrainWorkers final {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(120000); }
  } drain_workers;
  try {
    test_asset_backed_hdr_heal(runtime.path());
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "hdr_retouch_workflow_test passed\n";
  return 0;
}
