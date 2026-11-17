#include "desktop_pipeline_state_compiler.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_component_workspace_controller.h"
#include "photo_layer_compiler.h"
#include "photo_layer_source.h"
#include "photo_render_utils.h"
#include "photo_smart_document.h"
#include "photo_smart_object.h"
#include "raw_image_decoder.h"
#include "fixtures/synthetic_dng.h"

#include "photo_editor/io/openexr_io.h"
#include "photo_editor/pipeline/color_space.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QUrl>
#include <QVariantMap>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace photo_editor::desktop;
namespace smart_object = photo_editor::desktop::smart_object;
namespace component = photo_editor::desktop::smart_object::nested_document;

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }
};
}  // namespace photo_editor::desktop

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The editor recipe must parse before inspection.");
  return *parsed.state;
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      const auto found = layer_by_id(layer.value(key).toList(), id);
      if (!found.isEmpty()) return found;
    }
    const auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
    if (!document.isEmpty()) {
      const auto found = layer_by_id(document.value(QStringLiteral("layers")).toList(), id);
      if (!found.isEmpty()) return found;
    }
  }
  return {};
}

QString top_layer_id(const EditRecipeState& state, const QString& kind,
                     const QString& name = {}) {
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == kind &&
        (name.isEmpty() || layer.value(QStringLiteral("name")).toString() == name)) {
      return layer.value(QStringLiteral("id")).toString();
    }
  }
  return {};
}

int layer_index(const EditRecipeState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    static_cast<std::size_t>(a.width()) * 4U) != 0) return false;
  }
  return true;
}

QImage render(const QImage& base, const EditRecipeState& state) {
  const auto image = render_output_image_with_backend(
      base, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "A CPU render of the component workflow must be available.");
  return image;
}

void settle(EditorPreviewController& editor, const char* message) {
  QElapsedTimer timer;
  timer.start();
  const auto idle = [&editor]() {
    return !editor.processing() && !editor.fullRenderRunning() &&
        !editor.professionalToolsBusy() && !editor.photoPaintBusy() &&
        !editor.selectionWorkBusy() && !editor.photoExportBusy();
  };
  while (!idle() && timer.elapsed() < 90000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(idle(), message);
}

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

void wait_workspace(PhotoComponentWorkspaceController& workspace,
                    const char* message) {
  wait_until([&workspace]() { return workspace.active() && !workspace.busy(); },
             message);
}

void wait_closed(PhotoComponentWorkspaceController& workspace,
                 const char* message) {
  wait_until([&workspace]() { return !workspace.active() && !workspace.busy(); },
             message);
}

void restore_history(EditorPreviewController& editor, bool redo) {
  const auto asset_id = editor.assetId();
  bool finished = false;
  bool succeeded = false;
  const auto connection = QObject::connect(
      &editor, &EditorPreviewController::historyRestoreFinished, &editor,
      [&asset_id, &finished, &succeeded](const QString& restored_asset, bool success) {
        if (restored_asset != asset_id) return;
        finished = true;
        succeeded = success;
      });
  if (redo) editor.redo();
  else editor.undo();
  wait_until([&finished]() { return finished; },
             "History restore must emit its completion signal.");
  QObject::disconnect(connection);
  require(succeeded, "History restore must succeed.");
  settle(editor, "The restored document render must settle.");
}

QVariantMap without_document(QVariantMap layer) {
  layer.remove(QStringLiteral("smartObjectDocument"));
  return layer;
}

void verify_exact_source_file_decode(const QVariantMap& layer,
                                     const QByteArray& original_bytes,
                                     int expected_width = 4,
                                     int expected_height = 2) {
  QString error;
  const auto descriptor = layer.value(QStringLiteral("smartObject")).toMap();
  require(!descriptor.isEmpty() &&
              descriptor.value(QStringLiteral("payloadKind")).toString() ==
                  QStringLiteral("source-file") &&
              descriptor.value(QStringLiteral("policy")).toString() ==
                  QStringLiteral("embedded"),
          "The nested RAW/HDR source must remain an embedded original source-file payload.");
  require(smart_object::read_content_bytes(descriptor, &error) == original_bytes &&
              error.isEmpty(),
          "The nested source descriptor must preserve the exact original bytes.");
  const auto resolved = smart_object::resolve_instance_source(layer, &error);
  require(error.isEmpty() &&
              resolved.value(QStringLiteral("pixelData")).toByteArray() == original_bytes &&
              resolved.value(QStringLiteral("pixelDataEncoding")).toString() ==
                  QStringLiteral("source-file") &&
              resolved.value(QStringLiteral("smartObjectIntrinsicWidth")).toInt() == expected_width &&
              resolved.value(QStringLiteral("smartObjectIntrinsicHeight")).toInt() == expected_height,
          "Resolving the component source must retain bytes and decoded intrinsic geometry.");
}

void test_synthetic_dng_component(const QString& temporary_root) {
  if (!raw_camera_linear_decoder_capability().available) {
    std::cout << "SKIP: synthetic RAW Smart Object subcase; camera-linear RAW decoder unavailable\n";
    return;
  }
  const auto bytes = photo_editor::test_fixtures::synthetic_dng();
  const auto raw_path = QDir(temporary_root).filePath(QStringLiteral("synthetic-component-source.dng"));
  QFile raw_file(raw_path);
  require(raw_file.open(QIODevice::WriteOnly) &&
              raw_file.write(reinterpret_cast<const char*>(bytes.data()),
                  static_cast<qint64>(bytes.size())) == static_cast<qint64>(bytes.size()),
          "The repo-owned synthetic DNG source must be written exactly.");
  raw_file.close();
  const QByteArray original_bytes(reinterpret_cast<const char*>(bytes.data()),
                                  static_cast<qsizetype>(bytes.size()));
  const auto raw_size = source_image_size(raw_path);
  const auto raw_decode = decode_photo_layer_source_image(raw_path);
  require(raw_size == QSize(128, 128) && !raw_decode.isNull() &&
              raw_decode.size() == raw_size &&
              raw_decode.format() == QImage::Format_RGBA32FPx4,
          "The synthetic DNG must decode through the authoritative RAW path at its 128x128 sensor size.");

  QImage base(192, 160, QImage::Format_ARGB32);
  base.fill(QColor(28, 36, 44));
  base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto base_path = QDir(temporary_root).filePath(QStringLiteral("raw-component-base.png"));
  require(base.save(base_path, "PNG"), "The synthetic RAW component backdrop must be writable.");

  EditorPreviewController root;
  root.loadAsset(QStringLiteral("component-synthetic-raw-root"), base_path,
                 {}, {}, {}, {}, {}, true);
  settle(root, "The synthetic RAW component backdrop must render.");
  require(root.addSourceLayer(raw_path, QStringLiteral("Synthetic Bayer source")),
          "The parent must accept the synthetic DNG as an original source layer.");
  settle(root, "The synthetic DNG source layer must render.");
  auto state = recipe(root);
  const auto source_id = top_layer_id(state, QStringLiteral("source"));
  require(!source_id.isEmpty() &&
              root.setPhotoLayerTransform(layer_index(state, source_id), 6, 4, 1.0, 0.0),
          "The synthetic RAW source placement must remain editable.");
  settle(root, "The placed synthetic RAW source must render.");
  require(root.convertPhotoLayerToComponent(layer_index(recipe(root), source_id)),
          "Convert the synthetic DNG source to an editable component.");
  settle(root, "The synthetic DNG component must finish conversion.");

  auto component_layer = layer_by_id(recipe(root).layers, source_id);
  auto document = component_layer.value(QStringLiteral("smartObjectDocument")).toMap();
  auto source_child = layer_by_id(document.value(QStringLiteral("layers")).toList(), source_id);
  const auto descriptor = source_child.value(QStringLiteral("smartObject")).toMap();
  require(descriptor.value(QStringLiteral("intrinsicWidth")).toInt() == raw_size.width() &&
              descriptor.value(QStringLiteral("intrinsicHeight")).toInt() == raw_size.height(),
          "The embedded DNG descriptor must use the authoritative RAW sensor dimensions.");
  verify_exact_source_file_decode(source_child, original_bytes,
                                 raw_size.width(), raw_size.height());

  const auto before_apply = recipe(root);
  const auto before_pixels = render(base, before_apply);
  const auto history_before_apply = root.editHistoryCurrentIndex();
  PhotoComponentWorkspaceController workspace(&root);
  require(workspace.open(layer_index(before_apply, source_id)),
          "Open the embedded DNG component for a nested edit.");
  wait_workspace(workspace, "The synthetic DNG component workspace must open.");
  auto* child = workspace.controller();
  require(child != nullptr, "The synthetic DNG component must expose its child editor.");
  settle(*child, "The synthetic DNG child canvas must finish rendering.");
  require(child->addFillLayer(QStringLiteral("#d8e8f8"), QStringLiteral("RAW component glaze")),
          "Add an editable child layer above the immutable DNG source.");
  const auto glaze_id = top_layer_id(recipe(*child), QStringLiteral("fill"),
                                     QStringLiteral("RAW component glaze"));
  const auto glaze_index = layer_index(recipe(*child), glaze_id);
  require(glaze_index >= 0 && child->setPhotoLayerOpacity(glaze_index, 0.12),
          "Blend the synthetic RAW child edit without replacing its source payload.");
  settle(*child, "The synthetic RAW child edit must finish before Apply.");
  require(workspace.canApply() && workspace.apply(),
          "Apply the synthetic DNG component after exact-byte source decode preflight.");
  wait_closed(workspace, "The synthetic DNG component Apply must finish.");
  settle(root, "The synthetic RAW parent render must finish after Apply.");
  const auto applied = recipe(root);
  require(root.editHistoryCurrentIndex() == history_before_apply + 1 &&
              !same_pixels(render(base, applied), before_pixels),
          "Synthetic DNG Apply must change rendered pixels in exactly one parent history step.");
  component_layer = layer_by_id(applied.layers, source_id);
  document = component_layer.value(QStringLiteral("smartObjectDocument")).toMap();
  source_child = layer_by_id(document.value(QStringLiteral("layers")).toList(), source_id);
  verify_exact_source_file_decode(source_child, original_bytes,
                                 raw_size.width(), raw_size.height());

  EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("component-synthetic-raw-root"), base_path,
                     {}, {}, {}, {}, root.currentAssetBoundAdjustmentPatchJson(), true);
  settle(reopened, "The saved synthetic DNG component recipe must reopen.");
  const auto reopened_component = layer_by_id(recipe(reopened).layers, source_id);
  const auto reopened_document = reopened_component
      .value(QStringLiteral("smartObjectDocument")).toMap();
  const auto reopened_source = layer_by_id(
      reopened_document.value(QStringLiteral("layers")).toList(), source_id);
  verify_exact_source_file_decode(reopened_source, original_bytes,
                                 raw_size.width(), raw_size.height());
  require(recipe(reopened).layers == applied.layers &&
              same_pixels(render(base, recipe(reopened)), render(base, applied)),
          "Reopening the synthetic DNG component must retain its original bytes and rendered edits.");
}

void test_float_source_layer_decode(const QVariantMap& layer) {
  auto frame = layer;
  frame.insert(QStringLiteral("pixelWidth"), 8);
  frame.insert(QStringLiteral("pixelHeight"), 4);
  frame.insert(QStringLiteral("x"), 0);
  frame.insert(QStringLiteral("y"), 0);
  frame.insert(QStringLiteral("canvasWidth"), 8);
  frame.insert(QStringLiteral("canvasHeight"), 4);
  const auto compiled = compile_pipeline_photo_layers(
      {frame}, {}, LocalMaskRasterPolicy::UseStoredRaster,
      QSize(8, 4), std::span<const photo_editor::pipeline::ColorPixel>{},
      photo_editor::pipeline::ColorPipelineConfig{}, DesktopRenderPurpose::Output);
  if (compiled.size() != 1 || !compiled.front().content_error.empty() ||
      !compiled.front().effect_error.empty() || !compiled.front().style_error.empty() ||
      compiled.front().pixel_width != 8 || compiled.front().pixel_height != 4) {
    std::cerr << "Source-file compile diagnostic: requested=8x4 layer_count="
              << compiled.size();
    if (!compiled.empty()) {
      const auto& layer = compiled.front();
      std::cerr << " actual=" << layer.pixel_width << 'x' << layer.pixel_height
                << " pixel_count=" << layer.pixels.size()
                << " kind=" << layer.kind
                << " content_error='" << layer.content_error << "'"
                << " effect_error='" << layer.effect_error << "'"
                << " style_error='" << layer.style_error << "'";
    }
    std::cerr << '\n';
  }
  require(compiled.size() == 1 && compiled.front().content_error.empty() &&
              compiled.front().effect_error.empty() && compiled.front().style_error.empty() &&
              compiled.front().pixel_width == 8 && compiled.front().pixel_height == 4,
          "A source-file Smart Object must compile to its requested frame without decode errors.");
  const auto& pixels = compiled.front().pixels;
  require(pixels.size() == 32U, "The float HDR source frame must contain every pixel.");
  const auto has_extended_highlight = std::any_of(pixels.begin(), pixels.end(),
      [](const auto& pixel) {
        return std::max({pixel.red, pixel.green, pixel.blue}) > 1.25F &&
            pixel.alpha > 0.1F;
      });
  const auto has_signed_sample = std::any_of(pixels.begin(), pixels.end(),
      [](const auto& pixel) {
        return std::min({pixel.red, pixel.green, pixel.blue}) < -0.05F &&
            pixel.alpha > 0.1F;
      });
  const auto has_translucent_alpha = std::any_of(pixels.begin(), pixels.end(),
      [](const auto& pixel) { return pixel.alpha > 0.2F && pixel.alpha < 0.8F; });
  if (!has_extended_highlight || !has_signed_sample || !has_translucent_alpha) {
    auto rgb_min = std::numeric_limits<float>::infinity();
    auto rgb_max = -std::numeric_limits<float>::infinity();
    auto alpha_min = std::numeric_limits<float>::infinity();
    auto alpha_max = -std::numeric_limits<float>::infinity();
    for (const auto& pixel : pixels) {
      rgb_min = std::min(rgb_min, std::min({pixel.red, pixel.green, pixel.blue}));
      rgb_max = std::max(rgb_max, std::max({pixel.red, pixel.green, pixel.blue}));
      alpha_min = std::min(alpha_min, pixel.alpha);
      alpha_max = std::max(alpha_max, pixel.alpha);
    }
    std::cerr << "Float source value diagnostic: rgb_min=" << rgb_min
              << " rgb_max=" << rgb_max << " alpha_min=" << alpha_min
              << " alpha_max=" << alpha_max
              << " extended_highlight=" << has_extended_highlight
              << " signed_sample=" << has_signed_sample
              << " fractional_alpha=" << has_translucent_alpha << '\n';
  }
  require(has_extended_highlight && has_signed_sample && has_translucent_alpha,
          "Source-file rendering must retain values above one, negative values, and fractional alpha.");
}

void assert_export_matches(const QString& path, const QImage& expected) {
  const QImage exported(path);
  require(!exported.isNull() && exported.size() == expected.size(),
          "Flattened component export must retain the expected canvas dimensions.");
  const auto actual = exported.convertToFormat(QImage::Format_RGBA8888);
  const auto reference = expected.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < actual.height(); ++y) {
    for (int x = 0; x < actual.width(); ++x) {
      const auto a = actual.pixel(x, y);
      const auto b = reference.pixel(x, y);
      if (qAbs(qRed(a) - qRed(b)) > 2 || qAbs(qGreen(a) - qGreen(b)) > 2 ||
          qAbs(qBlue(a) - qBlue(b)) > 2 || qAbs(qAlpha(a) - qAlpha(b)) > 2) {
        require(false, "Flattened component export must match its decoded child pixels.");
      }
    }
  }
}

}  // namespace

int run_workflow() {
  QTemporaryDir temporary;
  require(temporary.isValid(), "The workflow must create isolated temporary storage.");
  test_synthetic_dng_component(temporary.path());
  if (!photo_editor::io::openexr_backend_available()) {
    std::cout << "SKIP: synthetic HDR Smart Object subcase; OpenEXR backend unavailable\n";
    return 0;
  }

  constexpr int kCanvasWidth = 48;
  constexpr int kCanvasHeight = 32;
  constexpr int kHdrWidth = 4;
  constexpr int kHdrHeight = 2;
  const std::vector<float> hdr_pixels{
      2.5F, 2.5F, 2.5F, 0.50F,
      -0.20F, -0.10F, -0.05F, 0.25F,
      -0.15F, -0.08F, -0.04F, 0.35F,
      0.2F, 0.4F, 0.6F, 0.75F,
      2.0F, 2.0F, 2.0F, 0.4F,
      -0.15F, -0.08F, -0.03F, 0.3F,
      -0.12F, -0.07F, -0.02F, 0.4F,
      0.1F, 0.2F, 0.3F, 0.6F};
  const auto source_path = temporary.filePath(QStringLiteral("component-source.exr"));
  std::string write_error;
  auto source_color_space = photo_editor::pipeline::make_linear_srgb_color_space();
  require(photo_editor::io::write_openexr_rgba(
              source_path.toStdString(), hdr_pixels,
              photo_editor::io::OpenExrWriteOptions{
                  .width = kHdrWidth,
                  .height = kHdrHeight,
                  .row_stride_floats = static_cast<std::size_t>(kHdrWidth) * 4U,
                  .half_float = false,
                  .include_alpha = true,
                  .color_space = source_color_space},
              &write_error),
          "The synthetic float EXR fixture must be writable.");

  QFile source_file(source_path);
  require(source_file.open(QIODevice::ReadOnly),
          "The original EXR fixture bytes must be readable.");
  const auto original_bytes = source_file.readAll();
  require(!original_bytes.isEmpty(), "The source fixture must have nonempty original bytes.");
  const auto decoded = decode_photo_layer_source_image(source_path);
  require(!decoded.isNull() && decoded.size() == QSize(kHdrWidth, kHdrHeight) &&
              decoded.format() == QImage::Format_RGBA32FPx4,
          "The EXR source decoder must preserve its intrinsic size and float representation.");
  const auto* first_row = reinterpret_cast<const QRgbaFloat32*>(decoded.constScanLine(0));
  require(qAbs(first_row[0].red() - 2.5F) < 2.0e-5F &&
              qAbs(first_row[0].alpha() - 0.5F) < 2.0e-5F &&
              first_row[1].red() < -0.19F,
          "The ordinary source decoder must retain highlight, signed, and alpha samples.");
  require(smart_object::supports_nested_source_file_format(QStringLiteral("shot.CR2")) &&
              smart_object::supports_nested_source_file_format(QStringLiteral("shot.exr")) &&
              !smart_object::supports_nested_source_file_format(QStringLiteral("shot.unsupported")),
          "Nested source classification must include RAW/HDR and reject unknown formats explicitly.");

  auto base = QImage(kCanvasWidth, kCanvasHeight, QImage::Format_ARGB32);
  base.fill(QColor(24, 32, 40));
  base.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto base_path = temporary.filePath(QStringLiteral("component-base.png"));
  require(base.save(base_path, "PNG"), "The component workflow base image must be writable.");

  EditorPreviewController root;
  root.loadAsset(QStringLiteral("component-raw-hdr-root"), base_path,
                 {}, {}, {}, {}, {}, true);
  settle(root, "The component parent source must finish rendering.");
  require(root.addSourceLayer(source_path, QStringLiteral("Signed HDR source")),
          "The parent must accept the original HDR source layer.");
  settle(root, "The source layer must finish its first full render.");
  auto state = recipe(root);
  const auto source_id = top_layer_id(state, QStringLiteral("source"));
  const auto source_index = layer_index(state, source_id);
  require(!source_id.isEmpty() && source_index >= 0 &&
              root.setPhotoLayerTransform(source_index, 5, 4, 1.0, 0.0),
          "The RAW/HDR source layer must retain its authored placement before conversion.");
  settle(root, "The placed HDR source must finish rendering.");
  require(root.convertPhotoLayerToComponent(layer_index(recipe(root), source_id)),
          "Convert the HDR source into a nested Smart Object component.");
  settle(root, "The source component must finish conversion and render.");

  auto component_layer = layer_by_id(recipe(root).layers, source_id);
  require(component_layer.value(QStringLiteral("kind")).toString() ==
              QStringLiteral("group"),
          "The converted HDR source must be an editable component layer.");
  auto document = component_layer.value(QStringLiteral("smartObjectDocument")).toMap();
  auto source_child = layer_by_id(document.value(QStringLiteral("layers")).toList(), source_id);
  require(source_child.value(QStringLiteral("smartObject")).toMap()
                  .value(QStringLiteral("intrinsicWidth")).toInt() == kHdrWidth &&
              source_child.value(QStringLiteral("smartObject")).toMap()
                  .value(QStringLiteral("intrinsicHeight")).toInt() == kHdrHeight,
          "The embedded source descriptor must use decoded HDR intrinsic dimensions.");
  require(source_child.value(QStringLiteral("x")).toInt() == 5 &&
              source_child.value(QStringLiteral("y")).toInt() == 4,
          "The nested HDR source placement must survive component conversion.");
  verify_exact_source_file_decode(source_child, original_bytes);
  test_float_source_layer_decode(source_child);

  const auto component_index = layer_index(recipe(root), source_id);
  require(component_index >= 0 &&
              root.setPhotoLayerTransform(component_index, 7, 3, 1.2, 11.0),
          "The outer component transform must be authored before editing its contents.");
  root.addMask(QStringLiteral("rectangle"));
  root.setActiveMaskFeather(0.0);
  root.setActiveMaskCenterX(0.51);
  root.setActiveMaskCenterY(0.48);
  root.setActiveMaskRadiusX(0.40);
  root.setActiveMaskRadiusY(0.35);
  require(root.applyWorkingSelectionToLayer(layer_index(recipe(root), source_id)) &&
              root.setPhotoLayerMaskOpacity(layer_index(recipe(root), source_id), 0.73),
          "The outer component must carry an authored selection-backed mask.");
  settle(root, "The outer transform and mask must finish rendering.");

  const auto before_cancel = recipe(root);
  const auto before_cancel_pixels = render(base, before_cancel);
  const auto before_cancel_history = root.editHistoryCurrentIndex();
  const auto source_child_before_apply = source_child;
  PhotoComponentWorkspaceController cancel_workspace(&root);
  require(cancel_workspace.open(layer_index(before_cancel, source_id)),
          "Open the HDR component in an isolated nested editor.");
  wait_workspace(cancel_workspace, "The HDR component workspace must open.");
  auto* cancel_child = cancel_workspace.controller();
  require(cancel_child && cancel_child != &root,
          "The HDR component must edit through a separate controller.");
  settle(*cancel_child, "The HDR child source must render in its isolated workspace.");
  const auto child_source = layer_by_id(
      recipe(*cancel_child).layers,
      source_child.value(QStringLiteral("id")).toString());
  const auto child_source_index = layer_index(
      recipe(*cancel_child), child_source.value(QStringLiteral("id")).toString());
  require(child_source_index >= 0 &&
              cancel_child->setPhotoLayerEnabled(child_source_index, false),
          "Author a child edit that Cancel must discard.");
  settle(*cancel_child, "The cancelled child edit must finish rendering.");
  require(cancel_workspace.cancel(), "Cancel the private HDR component draft.");
  wait_closed(cancel_workspace, "Cancel must close the nested HDR editor.");
  settle(root, "The parent must settle after component Cancel.");
  require(recipe(root) == before_cancel &&
              root.editHistoryCurrentIndex() == before_cancel_history &&
              same_pixels(render(base, recipe(root)), before_cancel_pixels),
          "Cancel must preserve parent HDR pixels, source bytes, mask, transform, and history.");

  PhotoComponentWorkspaceController workspace(&root);
  require(workspace.open(layer_index(recipe(root), source_id)),
          "Reopen the HDR component for Apply.");
  wait_workspace(workspace, "The HDR component must reopen after Cancel.");
  auto* child = workspace.controller();
  require(child && child != &root, "Apply must author inside the transient child editor.");
  settle(*child, "The reopened HDR child must finish rendering before edits.");
  require(child->addFillLayer(QStringLiteral("#ffffff"), QStringLiteral("HDR glaze")),
          "Add a reversible editable child layer above the original HDR source.");
  const auto glaze_id = top_layer_id(recipe(*child), QStringLiteral("fill"),
                                     QStringLiteral("HDR glaze"));
  const auto glaze_index = layer_index(recipe(*child), glaze_id);
  require(glaze_index >= 0 && child->setPhotoLayerOpacity(glaze_index, 0.18),
          "Blend the child layer without replacing the original float source.");
  settle(*child, "The HDR child edit must finish rendering before Apply.");
  const auto history_before_apply = root.editHistoryCurrentIndex();
  const auto outer_before_apply = layer_by_id(recipe(root).layers, source_id);
  require(workspace.canApply() && workspace.apply(),
          "Apply the edited HDR component after its source decode preflight.");
  wait_closed(workspace, "Apply must publish and close the HDR component workspace.");
  settle(root, "The parent HDR component must finish its Apply render.");
  const auto after_apply = recipe(root);
  const auto outer_after_apply = layer_by_id(after_apply.layers, source_id);
  document = outer_after_apply.value(QStringLiteral("smartObjectDocument")).toMap();
  source_child = layer_by_id(document.value(QStringLiteral("layers")).toList(),
                             source_child.value(QStringLiteral("id")).toString());
  require(root.editHistoryCurrentIndex() == history_before_apply + 1 &&
              document.value(QStringLiteral("revision")).toInt() >
                  component_layer.value(QStringLiteral("smartObjectDocument")).toMap()
                      .value(QStringLiteral("revision")).toInt(),
          "Apply must create one parent history step and advance the nested revision.");
  require(component::strip_transient_layer_view_fields({without_document(outer_before_apply)}) ==
              component::strip_transient_layer_view_fields({without_document(outer_after_apply)}),
          "Apply must preserve component identity, outer transform, masks, and all other placement fields.");
  const auto applied_glaze = layer_by_id(document.value(QStringLiteral("layers")).toList(), glaze_id);
  require(!applied_glaze.isEmpty(), "The child edit must be persisted in the component document.");
  require(source_child.value(QStringLiteral("x")).toInt() ==
                  source_child_before_apply.value(QStringLiteral("x")).toInt() &&
              source_child.value(QStringLiteral("y")).toInt() ==
                  source_child_before_apply.value(QStringLiteral("y")).toInt() &&
              source_child.value(QStringLiteral("scale")).toDouble() ==
                  source_child_before_apply.value(QStringLiteral("scale")).toDouble() &&
              source_child.value(QStringLiteral("rotation")).toDouble() ==
                  source_child_before_apply.value(QStringLiteral("rotation")).toDouble() &&
              source_child.value(QStringLiteral("smartObjectInstanceId")) ==
                  source_child_before_apply.value(QStringLiteral("smartObjectInstanceId")) &&
              source_child.value(QStringLiteral("smartObject")).toMap()
                  .value(QStringLiteral("contentId")) ==
                  source_child_before_apply.value(QStringLiteral("smartObject")).toMap()
                      .value(QStringLiteral("contentId")),
          "Nested source identity, placement, scale, and rotation must survive Apply.");
  verify_exact_source_file_decode(source_child, original_bytes);
  const auto applied_pixels = render(base, after_apply);
  require(!same_pixels(applied_pixels, before_cancel_pixels),
          "Applying the translucent child edit must change the parent output pixels.");

  restore_history(root, false);
  require(recipe(root) == before_cancel,
          "Undo must restore the exact prior component source, mask, transform, and nested revision.");
  restore_history(root, true);
  require(recipe(root) == after_apply,
          "Redo must restore the applied child edit and its embedded HDR source bytes.");

  const auto saved_patch = root.currentAssetBoundAdjustmentPatchJson();
  EditorPreviewController reopened;
  reopened.loadAsset(QStringLiteral("component-raw-hdr-root"), base_path,
                     {}, {}, {}, {}, saved_patch, true);
  settle(reopened, "The applied HDR component recipe must reopen in a fresh controller.");
  const auto reopened_component = layer_by_id(recipe(reopened).layers, source_id);
  const auto reopened_document = reopened_component
      .value(QStringLiteral("smartObjectDocument")).toMap();
  const auto reopened_source = layer_by_id(
      reopened_document.value(QStringLiteral("layers")).toList(),
      source_child.value(QStringLiteral("id")).toString());
  verify_exact_source_file_decode(reopened_source, original_bytes);
  require(recipe(reopened).layers == after_apply.layers &&
              same_pixels(render(base, recipe(reopened)), applied_pixels),
          "Save/reopen must preserve source bytes, nested edits, and rendered output.");

  PhotoComponentWorkspaceController export_workspace(&reopened);
  require(export_workspace.open(layer_index(recipe(reopened), source_id)),
          "Open the saved component to export its nested canvas.");
  wait_workspace(export_workspace, "The saved HDR component must open for export.");
  auto* export_child = export_workspace.controller();
  require(export_child != nullptr, "The saved component must expose its nested editor.");
  settle(*export_child, "The saved child canvas must settle before export.");
  const auto child_state = EditorPreviewControllerTestAccess::state(*export_child);
  QImage transparent(export_child->sourceImageWidth(), export_child->sourceImageHeight(),
                     QImage::Format_ARGB32_Premultiplied);
  transparent.fill(Qt::transparent);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
  const auto expected_export = render(transparent, child_state);
  const auto export_path = temporary.filePath(QStringLiteral("component-hdr-export.png"));
  require(export_child->startFlattenedPhotoExport(export_path),
          "Export the saved nested HDR component as a flattened PNG.");
  settle(*export_child, "The flattened HDR component export must finish.");
  require(export_child->photoExportError().isEmpty(),
          "The flattened HDR component export must not report an error.");
  assert_export_matches(export_path, expected_export);
  require(export_workspace.cancel(), "Close the export workspace without changing its document.");
  wait_closed(export_workspace, "The export workspace must close cleanly.");

  std::cout << "Smart Object RAW/HDR workflow passed: exact EXR bytes, float samples, alpha, dimensions, component Apply/Cancel, mask/transform, undo/redo, reopen, and export.\n";
  return 0;
}

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR",
          runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR",
          runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyComponentRawHdrTests"));
  QCoreApplication::setApplicationName(
      QStringLiteral("PhotoComponentRawHdr-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication application(argc, argv);
  struct DrainWorkers {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(90000); }
  } drain_workers;
  try {
    return run_workflow();
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
