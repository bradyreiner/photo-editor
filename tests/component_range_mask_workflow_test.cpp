#include "edit_recipe_v13_persistence.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "flattened_output_renderer.h"
#include "photo_component_workspace_controller.h"
#include "photo_render_utils.h"
#include "working_image.h"

#include <QBuffer>
#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <atomic>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
RenderResult render_preview_request_for_test(const RenderRequest& request);
}

namespace {

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

class ScopedEnvironment final {
 public:
  ScopedEnvironment(const char* name, QByteArray value)
      : name_(name), existed_(qEnvironmentVariableIsSet(name)), previous_(qgetenv(name)) {
    qputenv(name_, value);
  }
  ~ScopedEnvironment() {
    if (existed_) qputenv(name_, previous_);
    else qunsetenv(name_);
  }
 private:
  QByteArray name_;
  bool existed_ = false;
  QByteArray previous_;
};

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The component recipe must parse before render assertions.");
  return *parsed.state;
}

int layer_index(const EditRecipeState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) return index;
  }
  return -1;
}

QVariantMap mask_by_kind(const QVariantList& stack, const QString& kind) {
  for (auto index = stack.size(); index > 0; --index) {
    const auto mask = stack.at(index - 1).toMap();
    if (mask.value(QStringLiteral("kind")).toString() == kind) return mask;
  }
  return {};
}

void settle(EditorPreviewController& editor, const char* message) {
  QElapsedTimer timer;
  timer.start();
  const auto idle = [&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() &&
           !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
           !editor.selectionWorkBusy() && !editor.photoExportBusy();
  };
  while (!idle() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(idle(), message);
}

template <typename Predicate>
void wait_until(Predicate complete, const char* message) {
  QElapsedTimer timer;
  timer.start();
  while (!complete() && timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(complete(), message);
}

RenderResult render_state(const QString& path, const QString& asset, const EditRecipeState& state) {
  static std::atomic<int> generation{1};
  const auto next_generation = generation.fetch_add(1);
  RenderRequest request;
  request.source_asset_id = asset;
  request.file_path = path;
  request.render_reason = QStringLiteral("component_range_snapshot_test");
  request.state = state;
  request.full_resolution = true;
  request.max_edge = 0;
  request.use_in_memory_preview = true;
  request.require_cpu_preview_image = true;
  request.require_cpu_display_image = true;
  request.require_canonical_render_plan = true;
  request.generation = next_generation;
  request.latest_generation = std::make_shared<std::atomic<int>>(next_generation);
  return render_preview_request_for_test(request);
}

bool images_equal(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < a.height(); ++y) {
    const auto* ap = a.constScanLine(y);
    const auto* bp = b.constScanLine(y);
    for (int x = 0; x < a.width() * 4; ++x) {
      if (ap[x] != bp[x]) return false;
    }
  }
  return true;
}

int rgb_distance(const QColor& a, const QColor& b) {
  return std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
         std::abs(a.blue() - b.blue());
}

QByteArray png_bytes(const QImage& image) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  if (!buffer.open(QIODevice::WriteOnly)) return {};
  QImageWriter writer(&buffer, "png");
  writer.setCompression(1);
  return writer.write(image) ? bytes : QByteArray{};
}

void test_component_range_snapshot_preserves_soft_alpha() {
  QImage source(256, 1, QImage::Format_ARGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int x = 0; x < source.width(); ++x) {
    const auto alpha = x;
    const auto grey = 110 + (x * 30) / 255;
    source.setPixelColor(x, 0, QColor(grey, grey, grey, alpha));
  }
  const auto encoded = png_bytes(source);
  require(valid_component_range_source_snapshot(encoded, 256, 1, 1),
          "The 256-level alpha source must satisfy snapshot bounds.");

  const auto check_range = [&](double minimum, double maximum, double feather) {
    QVariantMap authored{{QStringLiteral("kind"), QStringLiteral("luminance")},
        {QStringLiteral("luminanceMin"), minimum},
        {QStringLiteral("luminanceMax"), maximum},
        {QStringLiteral("rangeFeather"), feather},
        {QStringLiteral("componentRangeSourceRequired"), true},
        {QStringLiteral("componentRangeSourceData"), encoded},
        {QStringLiteral("componentRangeSourceWidth"), 256},
        {QStringLiteral("componentRangeSourceHeight"), 1},
        {QStringLiteral("componentRangeSourceVersion"), 1}};
    const auto normalized = normalized_mask_definition(authored);
    const auto compiled = compile_pipeline_local_mask(
        normalized, LocalMaskRasterPolicy::UseStoredRaster, QSize(256, 1));
    require(compiled.kind == "brush" && compiled.segmentation_width == 256 &&
                compiled.segmentation_height == 1 && compiled.segmentation_mask.size() == 256,
            "A component range snapshot must compile as a literal soft-alpha raster.");

    auto unsnapshotted = authored;
    for (const auto* key : {"componentRangeSourceRequired", "componentRangeSourceData",
                            "componentRangeSourceWidth", "componentRangeSourceHeight",
                            "componentRangeSourceVersion"}) {
      unsnapshotted.remove(QString::fromLatin1(key));
    }
    const auto direct_mask = compile_pipeline_local_mask(
        normalized_mask_definition(unsnapshotted),
        LocalMaskRasterPolicy::UseStoredRaster, QSize(256, 1));
    photo_editor::pipeline::LocalMaskSet direct_set;
    direct_set.id = "range-reference";
    direct_set.sources.push_back(direct_mask);
    const auto working = working_image_from_qimage(source, QStringLiteral("Linear ProPhoto RGB"));
    require(!working.isNull(), "Decode the exact source colors used by the range reference.");
    const auto raw = photo_editor::pipeline::rasterize_local_mask_set_alpha(
        direct_set, working.width, working.height, working.pixels,
        photo_editor::pipeline::default_color_pipeline_config());
    require(raw.size() == compiled.segmentation_mask.size(),
            "The direct range reference must cover all 256 alpha levels.");
    for (int x = 0; x < 256; ++x) {
      const auto source_alpha = std::clamp(
          static_cast<double>(working.pixels[static_cast<std::size_t>(x)].alpha), 0.0, 1.0);
      const auto expected = static_cast<unsigned char>(std::lround(
          static_cast<double>(raw[static_cast<std::size_t>(x)]) * source_alpha));
      require(compiled.segmentation_mask[static_cast<std::size_t>(x)] == expected,
              "Snapshot compilation must preserve every soft-alpha byte and apply the same soft range weight.");
    }
  };

  // Full range proves byte-for-byte preservation across all 256 alpha values;
  // the narrow range proves that feathered Color/Luminance weighting is baked
  // once before the literal raster passes through normal selection handling.
  check_range(0.0, 1.0, 0.1);
  check_range(0.28, 0.72, 0.14);

  QImage colored_source(256, 1, QImage::Format_ARGB32);
  colored_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int x = 0; x < colored_source.width(); ++x) {
    colored_source.setPixelColor(x, 0, QColor(204, 72, 72, x));
  }
  const auto colored_encoded = png_bytes(colored_source);
  QVariantMap color_range{{QStringLiteral("kind"), QStringLiteral("color")},
      {QStringLiteral("colorSampleMode"), QStringLiteral("union")},
      {QStringLiteral("hueCenter"), 0.0}, {QStringLiteral("hueRange"), 0.08},
      {QStringLiteral("saturationMin"), 0.7}, {QStringLiteral("saturationMax"), 1.0},
      {QStringLiteral("rangeFeather"), 0.08},
      {QStringLiteral("colorSamples"), QVariantList{QVariantMap{
          {QStringLiteral("hue"), 0.0}, {QStringLiteral("saturation"), 0.89},
          {QStringLiteral("luminance"), 0.1}, {QStringLiteral("red"), 204},
          {QStringLiteral("green"), 72}, {QStringLiteral("blue"), 72},
          {QStringLiteral("hex"), QStringLiteral("#CC4848")}}}},
      {QStringLiteral("componentRangeSourceRequired"), true},
      {QStringLiteral("componentRangeSourceData"), colored_encoded},
      {QStringLiteral("componentRangeSourceWidth"), 256},
      {QStringLiteral("componentRangeSourceHeight"), 1},
      {QStringLiteral("componentRangeSourceVersion"), 1}};
  const auto normalized_color = normalized_mask_definition(color_range);
  const auto compiled_color = compile_pipeline_local_mask(
      normalized_color, LocalMaskRasterPolicy::UseStoredRaster, QSize(256, 1));
  require(compiled_color.kind == "brush" && compiled_color.segmentation_mask.size() == 256,
          "A union Color Range snapshot must compile to the bounded soft-alpha raster path.");
  for (int x = 0; x < 256; ++x) {
    require(compiled_color.segmentation_mask[static_cast<std::size_t>(x)] == x,
            "Union Color Range snapshot compilation must preserve each source-alpha level exactly.");
  }
}

void test_component_range_snapshot_render_and_persistence() {
  QTemporaryDir runtime;
  require(runtime.isValid(), "Create an isolated runtime for component range tests.");
  ScopedEnvironment disable_persistence("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  ScopedEnvironment app_data_root("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());

  constexpr int width = 192;
  constexpr int height = 128;
  QImage parent_source(width, height, QImage::Format_ARGB32);
  parent_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  parent_source.fill(QColor(QStringLiteral("#eeeeee")));
  const auto parent_path = QDir(runtime.path()).filePath(QStringLiteral("range-parent.png"));
  require(parent_source.save(parent_path, "PNG"), "Write the parent fixture.");

  QImage transparent(width, height, QImage::Format_ARGB32_Premultiplied);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent.fill(Qt::transparent);
  const auto transparent_path = QDir(runtime.path()).filePath(QStringLiteral("range-child.png"));
  require(transparent.save(transparent_path, "PNG"), "Write the transparent component source.");

  TestImageSink sink;
  EditorPreviewController parent;
  parent.setImageProvider(&sink);
  const auto parent_asset = QStringLiteral("component-range-snapshot-root");
  parent.loadAsset(parent_asset, parent_path, {}, {}, {}, {}, {});
  settle(parent, "The parent fixture must finish its initial render.");
  require(parent.addPhotoShape(QStringLiteral("rectangle"), 32, 20, 158, 108, width, height,
                               QStringLiteral("#552040"), QStringLiteral("#552040"), 0.0),
          "Add dark magenta artwork for Color and Luminance Range.");
  require(parent.addPhotoShape(QStringLiteral("rectangle"), 105, 20, 55, 108, width, height,
                               QStringLiteral("#205540"), QStringLiteral("#205540"), 0.0),
          "Add a second, disjoint green artwork region for multi-pick Color Range.");
  settle(parent, "The parent artwork must render before grouping and component conversion.");
  require(parent.groupPhotoLayers(QVariantList{0, 1}, QStringLiteral("Two-color artwork")),
          "Group both disjoint color regions into one editable component.");
  settle(parent, "The grouped color artwork must render before component conversion.");
  const auto before = recipe(parent);
  auto component_index = -1;
  auto component_id = QString();
  for (auto index = 0; index < before.layers.size(); ++index) {
    const auto layer = before.layers.at(index).toMap();
    if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("group")) {
      component_index = index;
      component_id = layer.value(QStringLiteral("id")).toString();
      break;
    }
  }
  require(component_index >= 0 && !component_id.isEmpty() &&
              parent.convertPhotoLayerToComponent(component_index),
          "Convert both grouped regions into an editable component document.");
  settle(parent, "The parent component instance must finish rendering.");

  PhotoComponentWorkspaceController workspace(&parent);
  require(workspace.open(layer_index(recipe(parent), component_id)),
          "Open the component's isolated editing workspace.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "The isolated component document must finish opening.");
  auto* child = workspace.controller();
  require(child != nullptr && child->assetId().startsWith(QStringLiteral("component-workspace-")),
          "The range test must use the transient component document identity.");
  settle(*child, "The component child document must finish rendering.");

  const auto child_before = recipe(*child);
  constexpr int sample_index_x = 60;
  constexpr int sample_index_y = 64;
  constexpr int second_sample_index_x = 132;
  constexpr int second_sample_index_y = 64;
  const auto sample_x = static_cast<double>(sample_index_x);
  const auto sample_y = static_cast<double>(sample_index_y);
  const auto normalized_x = (sample_x + 0.5) / width;
  const auto normalized_y = (sample_y + 0.5) / height;
  const auto second_normalized_x =
      (static_cast<double>(second_sample_index_x) + 0.5) / width;
  const auto second_normalized_y =
      (static_cast<double>(second_sample_index_y) + 0.5) / height;
  const auto initial_source = render_state(transparent_path, child->assetId(), child_before);
  require(initial_source.success && !initial_source.preview_image.isNull(),
          "The child source renderer must provide the composed appearance for range capture.");
  const auto visible_pixel = initial_source.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto second_visible_pixel = initial_source.preview_image.pixelColor(
      second_sample_index_x, second_sample_index_y);
  const auto transparent_corner = initial_source.preview_image.pixelColor(2, 2);
  const auto first_hue = visible_pixel.hsvHueF();
  const auto second_hue = second_visible_pixel.hsvHueF();
  auto hue_distance = std::abs(first_hue - second_hue);
  if (hue_distance > 0.5) hue_distance = 1.0F - hue_distance;
  const auto component_fixture_valid =
      first_hue >= 0.0 && second_hue >= 0.0 && hue_distance > 0.15 &&
      visible_pixel.hsvSaturationF() > 0.2 && visible_pixel.alpha() > 0 &&
      second_visible_pixel.hsvSaturationF() > 0.2 && second_visible_pixel.alpha() > 0 &&
      transparent_corner.alpha() == 0;
  if (!component_fixture_valid) {
    std::cerr << "component range source diagnostic: success=" << initial_source.success
              << " output=" << initial_source.preview_image.width() << 'x'
              << initial_source.preview_image.height()
              << " expected=" << width << 'x' << height
              << " failedStage=" << initial_source.failure_stage.toStdString()
              << " failedDetail=" << initial_source.failure_detail.toStdString()
              << " magenta=" << visible_pixel.red() << ',' << visible_pixel.green() << ','
              << visible_pixel.blue() << ',' << visible_pixel.alpha()
              << " green=" << second_visible_pixel.red() << ',' << second_visible_pixel.green() << ','
              << second_visible_pixel.blue() << ',' << second_visible_pixel.alpha()
              << " hues=" << first_hue << ',' << second_hue << " hueDistance=" << hue_distance
              << " corner=" << transparent_corner.red() << ',' << transparent_corner.green() << ','
              << transparent_corner.blue() << ',' << transparent_corner.alpha()
              << " layers=" << child_before.layers.size() << '\n';
    for (const auto& layer_value : child_before.layers) {
      const auto layer = layer_value.toMap();
      std::cerr << "  layer id=" << layer.value(QStringLiteral("id")).toString().toStdString()
                << " kind=" << layer.value(QStringLiteral("kind")).toString().toStdString()
                << " parent=" << layer.value(QStringLiteral("parentId")).toString().toStdString()
                << " x=" << layer.value(QStringLiteral("x")).toDouble()
                << " y=" << layer.value(QStringLiteral("y")).toDouble()
                << " size=" << layer.value(QStringLiteral("pixelWidth")).toInt() << 'x'
                << layer.value(QStringLiteral("pixelHeight")).toInt()
                << " scale=" << layer.value(QStringLiteral("scale")).toDouble()
                << " rotation=" << layer.value(QStringLiteral("rotation")).toDouble()
                << " opacity=" << layer.value(QStringLiteral("opacity")).toDouble()
                << " enabled=" << layer.value(QStringLiteral("enabled")).toBool() << '\n';
    }
  }
  require(component_fixture_valid,
          "The component fixture must contain two visible hues and a transparent corner.");

  child->addMask(QStringLiteral("color"));
  require(child->activeMaskKind() == QStringLiteral("color"), "Create a Color Range mask.");
  auto color_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  require(color_mask.value(QStringLiteral("componentRangeSourceRequired")).toBool() &&
              valid_component_range_source_snapshot(
                  color_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray(),
                  color_mask.value(QStringLiteral("componentRangeSourceWidth")).toInt(),
                  color_mask.value(QStringLiteral("componentRangeSourceHeight")).toInt(),
                  color_mask.value(QStringLiteral("componentRangeSourceVersion")).toInt()),
          "Component Color Range must capture a bounded full-canvas appearance at creation.");
  require(child->sampleActiveColorRangeAt(normalized_x, normalized_y),
          "Color Range must sample the first actual child hue.");
  settle(*child, "The first child color swatch must publish before editing its radius.");
  child->setActiveMaskHueRange(0.065);
  settle(*child, "The child hue radius must settle before sampling the second hue.");
  require(child->sampleActiveColorRangeAt(second_normalized_x, second_normalized_y),
          "Color Range must add a disjoint second child hue.");
  color_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  const auto color_snapshot = color_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray();
  require(valid_component_range_source_snapshot(
              color_snapshot,
              color_mask.value(QStringLiteral("componentRangeSourceWidth")).toInt(),
              color_mask.value(QStringLiteral("componentRangeSourceHeight")).toInt(),
              color_mask.value(QStringLiteral("componentRangeSourceVersion")).toInt()) &&
              !color_mask.value(QStringLiteral("colorSamples")).toList().isEmpty(),
          "Resampling must replace or refresh the persisted Color Range appearance snapshot.");
  require(color_mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
              color_mask.value(QStringLiteral("colorSamples")).toList().size() == 2 &&
              std::abs(color_mask.value(QStringLiteral("hueRange")).toDouble() - 0.065) < 1e-6,
          "The captured component range must retain both swatches and its editable per-pick radius.");
  const auto color_baseline = render_state(transparent_path, child->assetId(), recipe(*child));
  require(color_baseline.success, "Render the baseline Color Range component document.");
  const auto color_layer_index = child->ensureActiveSelectionAdjustmentLayer();
  require(color_layer_index >= 0 && child->setPhotoLayerAdjustmentValue(
              color_layer_index, QStringLiteral("exposure"), 1.0),
          "Attach a real editable adjustment to the Color Range selection.");
  const auto color_adjusted_state = recipe(*child);
  const auto color_adjusted = render_state(transparent_path, child->assetId(), color_adjusted_state);
  require(color_adjusted.success && color_adjusted.preview_image.size() == color_baseline.preview_image.size(),
          "Render the Color Range selection against its captured component appearance.");
  const auto color_before_pixel = color_baseline.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto color_after_pixel = color_adjusted.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto compiled_color_mask = compile_pipeline_local_mask(color_mask,
      LocalMaskRasterPolicy::UseStoredRaster, QSize(width, height));
  const auto color_coverage_index = static_cast<std::size_t>(sample_index_y * width + sample_index_x);
  const auto second_color_coverage_index =
      static_cast<std::size_t>(second_sample_index_y * width + second_sample_index_x);
  const auto color_coverage = color_coverage_index < compiled_color_mask.segmentation_mask.size()
      ? static_cast<int>(compiled_color_mask.segmentation_mask[color_coverage_index])
      : 0;
  const auto second_color_coverage =
      second_color_coverage_index < compiled_color_mask.segmentation_mask.size()
          ? static_cast<int>(compiled_color_mask.segmentation_mask[second_color_coverage_index])
          : 0;
  const auto source_snapshot = QImage::fromData(color_snapshot, "PNG");
  const auto snapshot_pixel = source_snapshot.isNull()
      ? QColor()
      : source_snapshot.pixelColor(sample_index_x, sample_index_y);
  auto direct_mask_definition = color_mask;
  for (const auto* key : {"componentRangeSourceRequired", "componentRangeSourceData",
                          "componentRangeSourceWidth", "componentRangeSourceHeight",
                          "componentRangeSourceVersion"}) {
    direct_mask_definition.remove(QString::fromLatin1(key));
  }
  const auto direct_local_mask = compile_pipeline_local_mask(
      normalized_mask_definition(direct_mask_definition),
      LocalMaskRasterPolicy::UseStoredRaster,
      source_snapshot.size());
  auto direct_working = working_image_from_qimage(
      source_snapshot, QStringLiteral("Linear ProPhoto RGB"));
  photo_editor::pipeline::LocalMaskSet direct_set;
  direct_set.id = "component-range-direct-diagnostic";
  direct_set.sources.push_back(direct_local_mask);
  const auto direct_alpha = source_snapshot.isNull() || direct_working.isNull()
      ? std::vector<std::uint8_t>{}
      : photo_editor::pipeline::rasterize_local_mask_set_alpha(
            direct_set, direct_working.width, direct_working.height, direct_working.pixels,
            photo_editor::pipeline::default_color_pipeline_config());
  const auto direct_coverage = color_coverage_index < direct_alpha.size()
      ? static_cast<int>(direct_alpha[color_coverage_index])
      : 0;
  const auto direct_second_coverage = second_color_coverage_index < direct_alpha.size()
      ? static_cast<int>(direct_alpha[second_color_coverage_index])
      : 0;
  std::cerr << "component_range_color_diagnostic"
            << " sample_xy=" << sample_index_x << ',' << sample_index_y
            << " baseline=" << color_before_pixel.red() << ',' << color_before_pixel.green() << ','
            << color_before_pixel.blue() << ',' << color_before_pixel.alpha()
            << " adjusted=" << color_after_pixel.red() << ',' << color_after_pixel.green() << ','
            << color_after_pixel.blue() << ',' << color_after_pixel.alpha()
            << " range_kind=" << compiled_color_mask.kind
            << " coverage=" << color_coverage
            << " second_coverage=" << second_color_coverage
            << " direct_coverage=" << direct_coverage
            << " direct_second_coverage=" << direct_second_coverage
            << " snapshot_pixel=" << snapshot_pixel.red() << ',' << snapshot_pixel.green() << ','
            << snapshot_pixel.blue() << ',' << snapshot_pixel.alpha()
            << " hue=" << color_mask.value(QStringLiteral("hueCenter")).toDouble()
            << " hue_range=" << color_mask.value(QStringLiteral("hueRange")).toDouble()
            << " sat=" << color_mask.value(QStringLiteral("saturationMin")).toDouble() << ':'
            << color_mask.value(QStringLiteral("saturationMax")).toDouble()
            << " layer_index=" << color_layer_index << '\n';
  const auto second_snapshot_pixel = source_snapshot.isNull()
      ? QColor()
      : source_snapshot.pixelColor(second_sample_index_x, second_sample_index_y);
  require(snapshot_pixel.alpha() == 255 && second_snapshot_pixel.alpha() == 255 &&
              color_coverage >= 250 && second_color_coverage >= 250 &&
              direct_coverage >= 250 && direct_second_coverage >= 250,
          "Both opaque child hues must receive full coverage from the captured Color Range union.");
  const auto second_color_before_pixel = color_baseline.preview_image.pixelColor(
      second_sample_index_x, second_sample_index_y);
  const auto second_color_after_pixel = color_adjusted.preview_image.pixelColor(
      second_sample_index_x, second_sample_index_y);
  require(rgb_distance(color_before_pixel, color_after_pixel) > 20 &&
              rgb_distance(second_color_before_pixel, second_color_after_pixel) > 20 &&
              color_adjusted.preview_image.pixelColor(2, 2).alpha() == 0,
          "Color Range must adjust both sampled child hues while preserving empty transparent pixels.");
  child->setActiveMaskSaturationMin(0.99);
  settle(*child, "The edited component Color Range tolerance must settle before the narrowed render.");
  const auto narrowed_color_state = recipe(*child);
  const auto narrowed_color_mask = narrowed_color_state.mask_stack.at(child->activeMaskIndex()).toMap();
  const auto narrowed_compiled_mask = compile_pipeline_local_mask(
      narrowed_color_mask, LocalMaskRasterPolicy::UseStoredRaster, QSize(width, height));
  const auto narrowed_coverage = color_coverage_index < narrowed_compiled_mask.segmentation_mask.size()
      ? static_cast<int>(narrowed_compiled_mask.segmentation_mask[color_coverage_index])
      : 0;
  const auto narrowed_color = render_state(transparent_path, child->assetId(), narrowed_color_state);
  const auto narrowed_color_pixel = narrowed_color.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto narrowed_color_delta = rgb_distance(color_before_pixel, narrowed_color_pixel);
  const auto narrowed_vs_adjusted_delta = rgb_distance(color_after_pixel, narrowed_color_pixel);
  const auto tolerance_reduced_coverage = narrowed_coverage + 100 < color_coverage;
  if (!narrowed_color.success || !tolerance_reduced_coverage || narrowed_vs_adjusted_delta <= 20) {
    std::cerr << "component_range_narrowed_color_diagnostic"
              << " success=" << narrowed_color.success
              << " size=" << narrowed_color.preview_image.width() << 'x'
              << narrowed_color.preview_image.height()
              << " saturation_min=" << narrowed_color_mask.value(QStringLiteral("saturationMin")).toDouble()
              << " saturation_max=" << narrowed_color_mask.value(QStringLiteral("saturationMax")).toDouble()
              << " hue_range=" << narrowed_color_mask.value(QStringLiteral("hueRange")).toDouble()
              << " coverage=" << narrowed_coverage
              << " baseline=" << color_before_pixel.red() << ',' << color_before_pixel.green() << ','
              << color_before_pixel.blue() << ',' << color_before_pixel.alpha()
              << " fully_selected=" << color_after_pixel.red() << ',' << color_after_pixel.green() << ','
              << color_after_pixel.blue() << ',' << color_after_pixel.alpha()
              << " narrowed=" << narrowed_color_pixel.red() << ',' << narrowed_color_pixel.green() << ','
              << narrowed_color_pixel.blue() << ',' << narrowed_color_pixel.alpha()
              << " baselineDelta=" << narrowed_color_delta
              << " fullySelectedDelta=" << narrowed_vs_adjusted_delta << '\n';
  }
  require(narrowed_color.success && tolerance_reduced_coverage && narrowed_vs_adjusted_delta > 20,
          "Changing the editable Color Range tolerance must change rendered union coverage.");
  require(child->setPhotoLayerAdjustmentValue(color_layer_index, QStringLiteral("exposure"), 0.0),
          "Reset the Color Range adjustment before the independent Luminance Range proof.");

  child->addMask(QStringLiteral("luminance"));
  require(child->activeMaskKind() == QStringLiteral("luminance"), "Create a Luminance Range mask.");
  auto luminance_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  require(luminance_mask.value(QStringLiteral("componentRangeSourceRequired")).toBool() &&
              valid_component_range_source_snapshot(
                  luminance_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray(),
                  luminance_mask.value(QStringLiteral("componentRangeSourceWidth")).toInt(),
                  luminance_mask.value(QStringLiteral("componentRangeSourceHeight")).toInt(),
                  luminance_mask.value(QStringLiteral("componentRangeSourceVersion")).toInt()),
          "Component Luminance Range must capture the rendered appearance before sampling.");
  require(child->beginActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->updateActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->commitActiveLuminanceRangeSample(normalized_x, normalized_y),
          "Luminance Range must sample and commit from the captured child appearance.");
  luminance_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  require(valid_component_range_source_snapshot(
              luminance_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray(),
              luminance_mask.value(QStringLiteral("componentRangeSourceWidth")).toInt(),
              luminance_mask.value(QStringLiteral("componentRangeSourceHeight")).toInt(),
              luminance_mask.value(QStringLiteral("componentRangeSourceVersion")).toInt()) &&
              !luminance_mask.value(QStringLiteral("luminanceSamples")).toList().isEmpty(),
          "Committed Luminance Range must retain the pixels used for its sample.");
  const auto luminance_baseline = render_state(transparent_path, child->assetId(), recipe(*child));
  require(luminance_baseline.success, "Render the baseline Luminance Range document.");
  const auto luminance_layer_index = child->ensureActiveSelectionAdjustmentLayer();
  require(luminance_layer_index >= 0 && child->setPhotoLayerAdjustmentValue(
              luminance_layer_index, QStringLiteral("exposure"), 1.0),
          "Attach an editable adjustment to the Luminance Range selection.");
  const auto luminance_adjusted = render_state(transparent_path, child->assetId(), recipe(*child));
  const auto luminance_before_pixel =
      luminance_baseline.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto luminance_after_pixel =
      luminance_adjusted.preview_image.pixelColor(sample_index_x, sample_index_y);
  const auto compiled_luminance_mask = compile_pipeline_local_mask(luminance_mask,
      LocalMaskRasterPolicy::UseStoredRaster, QSize(width, height));
  const auto luminance_coverage_index = static_cast<std::size_t>(sample_index_y * width + sample_index_x);
  const auto luminance_coverage = luminance_coverage_index < compiled_luminance_mask.segmentation_mask.size()
      ? static_cast<int>(compiled_luminance_mask.segmentation_mask[luminance_coverage_index])
      : 0;
  const auto luminance_snapshot = QImage::fromData(
      luminance_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray(), "PNG");
  const auto luminance_snapshot_pixel = luminance_snapshot.isNull()
      ? QColor()
      : luminance_snapshot.pixelColor(sample_index_x, sample_index_y);
  std::cerr << "component_range_luminance_diagnostic"
            << " sample_xy=" << sample_index_x << ',' << sample_index_y
            << " baseline=" << luminance_before_pixel.red() << ',' << luminance_before_pixel.green() << ','
            << luminance_before_pixel.blue() << ',' << luminance_before_pixel.alpha()
            << " adjusted=" << luminance_after_pixel.red() << ',' << luminance_after_pixel.green() << ','
            << luminance_after_pixel.blue() << ',' << luminance_after_pixel.alpha()
            << " range_kind=" << compiled_luminance_mask.kind
            << " coverage=" << luminance_coverage
            << " snapshot_pixel=" << luminance_snapshot_pixel.red() << ','
            << luminance_snapshot_pixel.green() << ',' << luminance_snapshot_pixel.blue() << ','
            << luminance_snapshot_pixel.alpha()
            << " range=" << luminance_mask.value(QStringLiteral("luminanceMin")).toDouble() << ':'
            << luminance_mask.value(QStringLiteral("luminanceMax")).toDouble()
            << " feather=" << luminance_mask.value(QStringLiteral("rangeFeather")).toDouble()
            << " layer_index=" << luminance_layer_index << '\n';
  require(luminance_snapshot_pixel.alpha() == 255 && luminance_coverage >= 250,
          "The opaque component pixel used for Luminance Range sampling must receive full range coverage.");
  require(luminance_adjusted.success &&
              rgb_distance(luminance_before_pixel, luminance_after_pixel) > 20 &&
              luminance_adjusted.preview_image.pixelColor(2, 2).alpha() == 0,
          "Luminance Range must adjust sampled component pixels and preserve transparent canvas pixels.");
  child->setActiveMaskLuminanceMin(1.0);
  const auto narrowed_luminance_state = recipe(*child);
  const auto narrowed_luminance_mask = narrowed_luminance_state.mask_stack.at(child->activeMaskIndex()).toMap();
  const auto narrowed_compiled_luminance = compile_pipeline_local_mask(
      narrowed_luminance_mask, LocalMaskRasterPolicy::UseStoredRaster, QSize(width, height));
  const auto narrowed_luminance_coverage =
      luminance_coverage_index < narrowed_compiled_luminance.segmentation_mask.size()
          ? static_cast<int>(narrowed_compiled_luminance.segmentation_mask[luminance_coverage_index])
          : 0;
  const auto narrowed_luminance = render_state(
      transparent_path, child->assetId(), narrowed_luminance_state);
  const auto narrowed_luminance_pixel = narrowed_luminance.preview_image.pixelColor(
      sample_index_x, sample_index_y);
  const auto narrowed_luminance_effect_delta = rgb_distance(luminance_after_pixel, narrowed_luminance_pixel);
  if (!narrowed_luminance.success || narrowed_luminance_coverage + 100 >= luminance_coverage ||
      narrowed_luminance_effect_delta <= 20) {
    std::cerr << "component_range_narrowed_luminance_diagnostic"
              << " success=" << narrowed_luminance.success
              << " range=" << narrowed_luminance_mask.value(QStringLiteral("luminanceMin")).toDouble() << ':'
              << narrowed_luminance_mask.value(QStringLiteral("luminanceMax")).toDouble()
              << " feather=" << narrowed_luminance_mask.value(QStringLiteral("rangeFeather")).toDouble()
              << " original_coverage=" << luminance_coverage
              << " narrowed_coverage=" << narrowed_luminance_coverage
              << " baseline=" << luminance_before_pixel.red() << ',' << luminance_before_pixel.green() << ','
              << luminance_before_pixel.blue() << ',' << luminance_before_pixel.alpha()
              << " fully_selected=" << luminance_after_pixel.red() << ',' << luminance_after_pixel.green() << ','
              << luminance_after_pixel.blue() << ',' << luminance_after_pixel.alpha()
              << " narrowed=" << narrowed_luminance_pixel.red() << ',' << narrowed_luminance_pixel.green() << ','
              << narrowed_luminance_pixel.blue() << ',' << narrowed_luminance_pixel.alpha()
              << " effect_delta=" << narrowed_luminance_effect_delta
              << " min_getter=" << child->activeMaskLuminanceMin()
              << " max_getter=" << child->activeMaskLuminanceMax() << '\n';
  }
  require(narrowed_luminance.success &&
              narrowed_luminance_coverage + 100 < luminance_coverage &&
              narrowed_luminance_effect_delta > 20,
          "Changing Luminance Range controls must reduce captured-source coverage and its attached adjustment.");

  const auto current_state = recipe(*child);
  const auto legacy_reopened = parse_adjustment_patch_json(child->currentAssetBoundAdjustmentPatchJson());
  require(legacy_reopened.ok(), "Serialize and reopen the component's legacy adjustment patch.");
  for (const auto& kind : {QStringLiteral("color"), QStringLiteral("luminance")}) {
    const auto saved = mask_by_kind(current_state.mask_stack, kind);
    const auto reopened = mask_by_kind(legacy_reopened.state->mask_stack, kind);
    require(saved.value(QStringLiteral("componentRangeSourceData")).toByteArray() ==
                reopened.value(QStringLiteral("componentRangeSourceData")).toByteArray() &&
                reopened.value(QStringLiteral("componentRangeSourceRequired")).toBool() &&
                (kind != QStringLiteral("color") ||
                 (reopened.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
                  reopened.value(QStringLiteral("colorSamples")).toList() ==
                      saved.value(QStringLiteral("colorSamples")).toList())),
            "Legacy recipe serialization must preserve the captured range source bytes.");
  }

  DesktopRecipeV13Context context;
  context.recipe_id = QStringLiteral("component-range-canonical-roundtrip");
  context.source_id = child->assetId();
  context.raw_source = false;
  context.decoder_provenance = QStringLiteral("raster");
  const RecipeAssetStore asset_store(QDir(runtime.path()).filePath(QStringLiteral("canonical-assets")));
  const auto serialized = serialize_desktop_recipe_as_canonical_v13_with_assets(
      current_state, context, asset_store);
  require(serialized.ok(), "Serialize component range masks to canonical asset-backed v13.");
  const auto parsed = photo_editor::pipeline::deserialize_edit_recipe_v13(
      serialized.document.recipe_payload.toStdString());
  require(parsed.has_value(), "Parse the canonical component recipe.");
  const auto reopened = project_canonical_v13_to_desktop_state_with_assets(*parsed, asset_store);
  require(reopened.ok(), "Reopen the canonical component mask payload assets.");
  for (const auto& kind : {QStringLiteral("color"), QStringLiteral("luminance")}) {
    const auto saved = mask_by_kind(current_state.mask_stack, kind);
    const auto reopened_mask = mask_by_kind(reopened.state->mask_stack, kind);
    require(saved.value(QStringLiteral("componentRangeSourceData")).toByteArray() ==
                reopened_mask.value(QStringLiteral("componentRangeSourceData")).toByteArray() &&
                reopened_mask.value(QStringLiteral("componentRangeSourceVersion")).toInt() == 1 &&
                (kind != QStringLiteral("color") ||
                 (reopened_mask.value(QStringLiteral("colorSampleMode")).toString() == QStringLiteral("union") &&
                  reopened_mask.value(QStringLiteral("colorSamples")).toList() ==
                      saved.value(QStringLiteral("colorSamples")).toList())),
            "Canonical recipe assets must preserve each captured range appearance.");
  }
  const auto reopened_render = render_state(transparent_path, child->assetId(), *reopened.state);
  const auto export_image = render_flattened_output_file(
      child->assetId(), transparent_path, *reopened.state,
      QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA8888);
  require(reopened_render.success && !export_image.isNull() &&
              images_equal(reopened_render.preview_image, export_image),
          "Canonical reopen, desktop preview and actual export must render identical component range pixels.");
  require(export_image.pixelColor(2, 2).alpha() == 0,
          "Canonical range export must preserve fully transparent child canvas pixels.");

  auto malformed = color_mask;
  malformed.insert(QStringLiteral("componentRangeSourceRequired"), true);
  malformed.insert(QStringLiteral("componentRangeSourceData"), QByteArrayLiteral("not-a-png"));
  const auto malformed_normalized = normalized_mask_definition(malformed);
  require(malformed_normalized.value(QStringLiteral("componentRangeSourceRequired")).toBool() &&
              malformed_normalized.value(QStringLiteral("componentRangeSourceData")).toByteArray().isEmpty(),
          "Malformed snapshot bytes must remain marked as required and must not be treated as a legacy range mask.");
  const auto malformed_compiled = compile_pipeline_local_mask(malformed_normalized);
  require(malformed_compiled.kind == "brush" && malformed_compiled.segmentation_width == 1 &&
              malformed_compiled.segmentation_height == 1 &&
              malformed_compiled.segmentation_mask == std::vector<std::uint8_t>{0},
          "Malformed component snapshots must compile to fail-closed empty coverage.");

  require(workspace.cancel(), "Close the isolated range workspace without applying its draft.");
  wait_until([&workspace]() { return !workspace.busy() && !workspace.active(); },
             "The range workspace must cancel cleanly after proof.");
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  try {
    test_component_range_snapshot_preserves_soft_alpha();
    test_component_range_snapshot_render_and_persistence();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Component range snapshot workflow passed.\n";
  return 0;
}
