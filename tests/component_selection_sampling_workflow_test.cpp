#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_component_workspace_controller.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
QImage smart_select_source_image_for_test(
    const QString& file_path,
    const QString& asset_id,
    const EditState& state);
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
      : name_(name),
        existed_(qEnvironmentVariableIsSet(name)),
        previous_(qgetenv(name)) {
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
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The test recipe must parse before it is inspected.");
  return *parsed.state;
}

int layer_index(const EditRecipeState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id) {
      return index;
    }
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

QByteArray mask_bytes(const QVariantMap& mask) {
  return mask.value(QStringLiteral("segmentationData")).toByteArray();
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

void test_component_selection_sampling() {
  QTemporaryDir runtime;
  require(runtime.isValid(), "Create a private runtime directory for the component test.");
  ScopedEnvironment disable_persistence(
      "PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  ScopedEnvironment app_data_root("PHOTO_EDITOR_APP_DATA_DIR",
                                  runtime.path().toUtf8());
  ScopedEnvironment fake_segmentation(
      "PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", QByteArrayLiteral("1"));

  constexpr int width = 192;
  constexpr int height = 128;
  QImage base(width, height, QImage::Format_ARGB32);
  base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  base.fill(QColor(QStringLiteral("#f0f0f0")));
  const auto base_path = QDir(runtime.path()).filePath(QStringLiteral("base.png"));
  require(base.save(base_path, "PNG"), "Write the ordinary parent source fixture.");

  QImage transparent(width, height, QImage::Format_ARGB32_Premultiplied);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent.fill(Qt::transparent);
  const auto transparent_path =
      QDir(runtime.path()).filePath(QStringLiteral("transparent-component.png"));
  require(transparent.save(transparent_path, "PNG"),
          "Write the transparent full-canvas source fixture.");

  TestImageSink sink;
  EditorPreviewController parent;
  parent.setImageProvider(&sink);
  const auto parent_asset = QStringLiteral("component-selection-sampling-root");
  parent.loadAsset(parent_asset, base_path, {}, {}, {}, {}, {});
  settle(parent, "The parent source preview must finish before conversion.");
  require(parent.addPhotoShape(
              QStringLiteral("rectangle"), 28, 16, 160, 112, width, height,
              QStringLiteral("#f020dc"), QStringLiteral("#f020dc"), 0.0),
          "Add authored artwork that can be sampled inside the child document.");
  settle(parent, "The parent artwork must render before conversion.");
  const auto parent_state = recipe(parent);
  require(!parent_state.layers.isEmpty(), "The parent shape layer must exist.");
  const auto shape_id = parent_state.layers.front().toMap()
                            .value(QStringLiteral("id")).toString();
  const auto parent_shape_index = layer_index(parent_state, shape_id);
  require(!shape_id.isEmpty() && parent_shape_index >= 0 &&
              parent.convertPhotoLayerToComponent(parent_shape_index),
          "Convert the sample artwork into an editable component.");
  settle(parent, "The converted component must finish rendering.");

  PhotoComponentWorkspaceController workspace(&parent);
  require(workspace.open(layer_index(recipe(parent), shape_id)),
          "Open the converted component in its isolated workspace.");
  wait_until([&workspace]() { return !workspace.busy() && workspace.active(); },
             "The component workspace must finish opening.");
  auto* child = workspace.controller();
  require(child != nullptr && child != &parent && child->assetId().startsWith(
              QStringLiteral("component-workspace-")),
          "The test must exercise a real transient component canvas.");
  settle(*child, "The transparent child canvas and authored shape must finish rendering.");

  const auto child_state = recipe(*child);
  const auto child_shape_index = layer_index(child_state, shape_id);
  require(child_shape_index >= 0, "The converted shape must remain in the child document.");
  const auto shape = child_state.layers.at(child_shape_index).toMap();
  const auto geometry_center_x = shape.value(QStringLiteral("x")).toDouble() +
      shape.value(QStringLiteral("pixelWidth")).toDouble() *
          shape.value(QStringLiteral("scale"), 1.0).toDouble() * 0.5;
  const auto geometry_center_y = shape.value(QStringLiteral("y")).toDouble() +
      shape.value(QStringLiteral("pixelHeight")).toDouble() *
          shape.value(QStringLiteral("scale"), 1.0).toDouble() * 0.5;
  const auto source = smart_select_source_image_for_test(
      transparent_path, child->assetId(), child_state);
  const auto diagnostic_path = QDir(runtime.path()).filePath(
      QStringLiteral("component-sampling-composed-source.png"));
  if (!source.isNull()) source.save(diagnostic_path, "PNG");
  require(source.size() == QSize(width, height),
          "The component sampler must preserve the full canvas dimensions.");
  const auto geometry_pixel_x = qBound(0, static_cast<int>(std::lround(geometry_center_x)), width - 1);
  const auto geometry_pixel_y = qBound(0, static_cast<int>(std::lround(geometry_center_y)), height - 1);
  const auto geometry_pixel = source.pixelColor(geometry_pixel_x, geometry_pixel_y);
  auto min_x = width;
  auto min_y = height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < source.height(); ++y) {
    for (int x = 0; x < source.width(); ++x) {
      const auto pixel = source.pixelColor(x, y);
      if (pixel.alpha() > 0 && pixel.red() > 150 && pixel.blue() > 150 &&
          pixel.green() < std::min(pixel.red(), pixel.blue()) * 0.55) {
        min_x = std::min(min_x, x);
        min_y = std::min(min_y, y);
        max_x = std::max(max_x, x);
        max_y = std::max(max_y, y);
      }
    }
  }
  std::cerr << "component_sampling_source_diagnostic"
            << " size=" << source.width() << 'x' << source.height()
            << " geometry_center=" << geometry_center_x << ',' << geometry_center_y
            << " geometry_pixel=" << geometry_pixel_x << ',' << geometry_pixel_y
            << " geometry_rgb=" << geometry_pixel.red() << ',' << geometry_pixel.green() << ','
            << geometry_pixel.blue() << ',' << geometry_pixel.alpha()
            << " corner_rgb=" << source.pixelColor(2, 2).red() << ','
            << source.pixelColor(2, 2).green() << ',' << source.pixelColor(2, 2).blue() << ','
            << source.pixelColor(2, 2).alpha()
            << " magenta_bounds=" << min_x << ',' << min_y << ':' << max_x << ',' << max_y
            << " image=" << diagnostic_path.toStdString() << '\n';
  require(max_x >= min_x && max_y >= min_y,
          "The composed child source must contain the known authored magenta pixels; inspect the saved diagnostic image.");
  const auto sample_x = (static_cast<double>(min_x + max_x) * 0.5);
  const auto sample_y = (static_cast<double>(min_y + max_y) * 0.5);
  auto sample_pixel_x = qBound(0, static_cast<int>(std::lround(sample_x)), width - 1);
  auto sample_pixel_y = qBound(0, static_cast<int>(std::lround(sample_y)), height - 1);
  if (source.pixelColor(sample_pixel_x, sample_pixel_y).alpha() == 0 ||
      source.pixelColor(sample_pixel_x, sample_pixel_y).green() >= 100) {
    sample_pixel_x = min_x;
    sample_pixel_y = min_y;
  }
  const auto artwork = source.pixelColor(sample_pixel_x, sample_pixel_y);
  const auto normalized_x = static_cast<double>(sample_pixel_x) / (width - 1);
  const auto normalized_y = static_cast<double>(sample_pixel_y) / (height - 1);
  require(artwork.red() > 150 && artwork.blue() > 150 && artwork.green() < 80 &&
              source.pixelColor(2, 2).alpha() == 0,
          "The composed sampling source must contain visible child pixels and retain transparent canvas pixels.");

  child->addMask(QStringLiteral("color"));
  require(child->activeMaskKind() == QStringLiteral("color"),
          "Color Range creation must be available inside a component.");
  require(child->sampleActiveColorRangeAt(normalized_x, normalized_y),
          "Color Range must sample the composed child artwork.");
  const auto color_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  const auto color_samples = color_mask.value(QStringLiteral("colorSamples")).toList();
  require(!color_samples.isEmpty(), "Color Range must persist its sampled patch.");
  const auto color_sample = color_samples.back().toMap();
  require(color_sample.value(QStringLiteral("red")).toInt() == artwork.red() &&
              color_sample.value(QStringLiteral("green")).toInt() == artwork.green() &&
              color_sample.value(QStringLiteral("blue")).toInt() == artwork.blue(),
          "Color Range must record the exact composed-source pixel selected by normalized coordinates.");

  child->addMask(QStringLiteral("luminance"));
  require(child->activeMaskKind() == QStringLiteral("luminance"),
          "Luminance Range creation must be available inside a component.");
  const auto masks_before_cancel = child->maskStack();
  require(child->beginActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->updateActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->cancelActiveLuminanceRangeSample() &&
              child->maskStack() == masks_before_cancel,
          "Canceling a component Luminance Range sample must leave its recipe unchanged.");
  require(child->beginActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->updateActiveLuminanceRangeSample(normalized_x, normalized_y) &&
              child->commitActiveLuminanceRangeSample(normalized_x, normalized_y),
          "Luminance Range must sample and commit from the composed child artwork.");
  auto luminance_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  auto luminance_samples = luminance_mask.value(QStringLiteral("luminanceSamples")).toList();
  const auto linear_channel = [](double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92 : std::pow((encoded + 0.055) / 1.055, 2.4);
  };
  const auto expected_luminance = 0.2126 * linear_channel(artwork.redF()) +
      0.7152 * linear_channel(artwork.greenF()) + 0.0722 * linear_channel(artwork.blueF());
  require(!luminance_samples.isEmpty() &&
              luminance_samples.back().toMap().value(QStringLiteral("pixelCount")).toInt() > 0 &&
              std::abs(luminance_samples.back().toMap().value(QStringLiteral("luminanceMean")).toDouble() -
                       expected_luminance) < 0.001,
          "Luminance Range must record linear-light luminance of the visible child artwork.");
  child->setActiveMaskLuminanceMin(0.31);
  luminance_mask = child->maskStack().at(child->activeMaskIndex()).toMap();
  require(std::abs(luminance_mask.value(QStringLiteral("luminanceMin")).toDouble() - 0.31) < 1e-9,
          "Luminance Range controls must remain editable in a component workspace.");

  // The fake backend only proves that captured component pixels reach the
  // generated-selection plumbing. It does not qualify semantic model quality.
  child->addMask(QStringLiteral("subject"));
  child->addAIMask(QStringLiteral("sky"));
  child->addAIMask(QStringLiteral("background"));
  auto masks = child->maskStack();
  for (const auto& kind : {QStringLiteral("subject"), QStringLiteral("sky"),
                           QStringLiteral("background")}) {
    const auto generated = mask_by_kind(masks, kind);
    require(!generated.isEmpty() && !mask_bytes(generated).isEmpty() &&
                generated.value(QStringLiteral("segmentationWidth")).toInt() > 0 &&
                generated.value(QStringLiteral("segmentationHeight")).toInt() > 0,
            "Generated Subject, Sky and Background must create editable child-canvas masks.");
  }
  require(child->staleGeneratedMaskCount() == 0,
          "A generated component selection must be current immediately after publication.");

  const auto subject = mask_by_kind(masks, QStringLiteral("subject"));
  const auto subject_data = mask_bytes(subject);
  const auto subject_width = subject.value(QStringLiteral("segmentationWidth")).toInt();
  const auto subject_height = subject.value(QStringLiteral("segmentationHeight")).toInt();
  const auto mask_x = qBound(0, static_cast<int>(std::lround(
                                  sample_x * (subject_width - 1) / (width - 1))),
                             subject_width - 1);
  const auto mask_y = qBound(0, static_cast<int>(std::lround(
                                  sample_y * (subject_height - 1) / (height - 1))),
                             subject_height - 1);
  const auto subject_at_artwork = static_cast<unsigned char>(
      subject_data.at(mask_y * subject_width + mask_x));
  require(subject_at_artwork > 0,
          "The generated mask raster must cover the known rendered child artwork pixel.");
  const auto sky = mask_by_kind(masks, QStringLiteral("sky"));
  require(static_cast<unsigned char>(mask_bytes(sky).at(0)) == 0,
          "Generated masks must exclude fully transparent component pixels even when the fixture backend marks them.");

  const auto shape_index = layer_index(recipe(*child), shape_id);
  require(shape_index >= 0 && child->setPhotoLayerShapeStyle(
              shape_index, QStringLiteral("#27d4b1"), QStringLiteral("#27d4b1"), 0.0),
          "Change authored child artwork so generated selection freshness can be checked.");
  require(child->staleGeneratedMaskCount() == 3,
          "Generated component masks must become stale when their rendered child source changes.");
  require(child->updateStaleGeneratedMasks() == 3 &&
              child->staleGeneratedMaskCount() == 0,
          "Refreshing stale component selections must resample the current composed canvas.");

  require(workspace.cancel(), "Close the isolated component sampling test without applying it.");
  wait_until([&workspace]() { return !workspace.busy() && !workspace.active(); },
             "Cancel must close the component sampling workspace.");
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication application(argc, argv);
  try {
    test_component_selection_sampling();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
  std::cout << "Component selection sampling workflow passed.\n";
  return 0;
}
