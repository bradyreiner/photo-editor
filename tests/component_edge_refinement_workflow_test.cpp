#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_component_workspace_controller.h"
#include "photo_render_utils.h"

#include <QColor>
#include <QCoreApplication>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
QImage smart_select_source_image_for_test(
    const QString& file_path, const QString& asset_id, const EditState& state);

struct EditorPreviewControllerTestAccess {
  static QString filePath(const EditorPreviewController& controller) {
    return controller.file_path_;
  }
  static BrushLivePreviewFrame liveBrushFrame(
      const EditorPreviewController& controller) {
    return controller.active_brush_live_preview_frame_;
  }
};
}  // namespace photo_editor::desktop

namespace {

void require(bool condition, const QString& message) {
  if (!condition) throw std::runtime_error(message.toStdString());
}

EditState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), QStringLiteral("The component recipe must decode."));
  return *parsed.state;
}

int layer_index(const EditState& state, const QString& id) {
  for (int index = 0; index < state.layers.size(); ++index) {
    if (state.layers.at(index).toMap().value(QStringLiteral("id")).toString() == id)
      return index;
  }
  return -1;
}

QString component_layer_id(const EditState& state) {
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (!layer.value(QStringLiteral("smartObjectDocument")).toMap().isEmpty())
      return layer.value(QStringLiteral("id")).toString();
  }
  return {};
}

void settle(EditorPreviewController& editor, const QString& context) {
  QElapsedTimer timer;
  timer.start();
  while ((editor.processing() || editor.fullRenderRunning() ||
          editor.professionalToolsBusy() || editor.selectionWorkBusy() ||
          editor.brushStrokeActive() || editor.photoPaintBusy()) &&
         timer.elapsed() < 60000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(!editor.processing() && !editor.fullRenderRunning() &&
              !editor.professionalToolsBusy() && !editor.selectionWorkBusy() &&
              !editor.brushStrokeActive() && !editor.photoPaintBusy(),
          context);
}

template <typename Predicate>
void wait_until(Predicate done, const QString& context, int timeout_ms = 60000) {
  QElapsedTimer timer;
  timer.start();
  while (!done() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  require(done(), context);
}

EditorPreviewController* open_child(PhotoComponentWorkspaceController& workspace,
                                    EditorPreviewController& parent,
                                    const QString& component_id) {
  const auto index = layer_index(recipe(parent), component_id);
  require(index >= 0 && workspace.open(index),
          QStringLiteral("Open component contents in the isolated editor."));
  wait_until([&workspace] { return !workspace.busy() && workspace.active(); },
             QStringLiteral("Wait for component child initialization."));
  auto* child = workspace.controller();
  require(child && child != &parent && child->hasAsset(),
          QStringLiteral("Component contents must have an independent source canvas."));
  settle(*child, QStringLiteral("Wait for the component child preview."));
  return child;
}

void add_component_artwork(EditorPreviewController& child) {
  const auto current = recipe(child);
  for (int index = static_cast<int>(current.layers.size()); index > 0; --index) {
    const auto layer = current.layers.at(index - 1).toMap();
    if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("fill") &&
        layer.value(QStringLiteral("enabled"), true).toBool()) {
      require(child.setPhotoLayerEnabled(index - 1, false),
              QStringLiteral("Hide the original full-canvas seed layer."));
    }
  }
  const auto width = child.sourceImageWidth();
  const auto height = child.sourceImageHeight();
  require(width >= 128 && height >= 96,
          QStringLiteral("The synthetic component source must preserve its canvas size."));
  require(child.addPhotoShape(QStringLiteral("rectangle"), 16, 14, 61, 81,
              width, height, QStringLiteral("#f320c8"), {}, 0.0),
          QStringLiteral("Add magenta artwork to the transparent component canvas."));
  require(child.addPhotoShape(QStringLiteral("rectangle"), 66, 14, 112, 81,
              width, height, QStringLiteral("#13d9ed"), {}, 0.0),
          QStringLiteral("Add a distinct cyan region for color-aware refinement."));
  settle(child, QStringLiteral("Wait for component artwork to render."));
}

QImage component_source(EditorPreviewController& child) {
  const auto image = smart_select_source_image_for_test(
      EditorPreviewControllerTestAccess::filePath(child), child.assetId(), recipe(child));
  require(!image.isNull(), QStringLiteral("Render the composed component source for the test."));
  return image;
}

QImage render_component_state(const EditState& state, const QSize& size) {
  QImage transparent(size, QImage::Format_ARGB32);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent.fill(Qt::transparent);
  const auto image = render_output_image_with_backend(
      transparent, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly, nullptr, nullptr,
      RenderMaskOptions{.rebuild_editable_masks_at_output_resolution = true});
  require(!image.isNull() && image.size() == size,
          QStringLiteral("Canonical component rendering must produce the full-resolution canvas."));
  return image;
}

int color_distance(const QColor& left, const QColor& right) {
  return std::abs(left.red() - right.red()) +
      std::abs(left.green() - right.green()) +
      std::abs(left.blue() - right.blue()) +
      std::abs(left.alpha() - right.alpha());
}

QVariantMap edge_refine_mask(const EditorPreviewController& child) {
  for (const auto& value : child.maskStack()) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("brushRefineMode")).toBool() &&
        mask.value(QStringLiteral("kind")).toString() == QStringLiteral("brush"))
      return mask;
  }
  return {};
}

bool brush_history_uses_source_sampling(const QVariantMap& mask) {
  if (mask.value(QStringLiteral("brushAutoMask")).toBool() ||
      mask.value(QStringLiteral("brushProtectEdges")).toBool()) return true;
  for (const auto& value : mask.value(QStringLiteral("brushStrokes")).toList()) {
    const auto settings = value.toMap().value(QStringLiteral("settings")).toMap();
    if (settings.value(QStringLiteral("autoMask")).toBool() ||
        settings.value(QStringLiteral("protectEdges")).toBool()) return true;
  }
  return false;
}

void prepare_edge_refine(EditorPreviewController& child) {
  child.addMask(QStringLiteral("rectangle"));
  require(child.hasActiveMask(),
          QStringLiteral("Create an independent working selection."));
  child.setActiveMaskFeather(0.0);
  // Keep the source selection over the magenta component only. This lets the
  // actual persisted parent + Edge Refine child set prove that sampled
  // coverage stays off the adjacent cyan artwork.
  child.setActiveMaskCenterX(0.30);
  child.setActiveMaskCenterY(0.5);
  child.setActiveMaskRadiusX(0.20);
  child.setActiveMaskRadiusY(0.47);
  settle(child, QStringLiteral("Settle the working selection."));
  child.startSelectionBrush(false);
  settle(child, QStringLiteral("Create a freehand selection brush before edge refinement."));
  child.setActiveMaskBrushAutoMask(true);
  child.setActiveMaskBrushProtectEdges(true);
  require(!child.activeMaskBrushAutoMask() && !child.activeMaskBrushProtectEdges(),
          QStringLiteral("Ordinary component brushes must still reject source-sampled settings."));
  child.startSelectionEdgeBrush(false);
  require(!edge_refine_mask(child).isEmpty() && child.activeMaskBrushAutoMask() &&
              child.activeMaskBrushProtectEdges(),
          QStringLiteral("Edge Refine must create the supported source-aware component brush."));
  require(child.applyActiveMaskBrushPreset(QStringLiteral("Edge Aware")) &&
              child.activeMaskBrushAutoMask() && child.activeMaskBrushProtectEdges(),
          QStringLiteral("Edge presets must be available on the supported component brush."));
  child.setActiveMaskBrushAutoMaskTolerance(0.14);
  child.setActiveMaskBrushAutoMaskContinuousSampling(false);
  child.setActiveMaskBrushProtectEdges(true);
  require(std::abs(child.activeMaskBrushAutoMaskTolerance() - 0.14) < 1e-8 &&
              child.activeMaskBrushProtectEdges(),
          QStringLiteral("Source-sampled settings must remain editable for Edge Refine."));
  settle(child, QStringLiteral("Settle the Edge Refine tool setup."));
}

BrushLivePreviewFrame preview_edge_refine(EditorPreviewController& child) {
  const auto prior_revision = child.activeMaskLiveBrushPreviewRevision();
  child.beginActiveBrushStroke();
  require(child.brushStrokeActive(),
          QStringLiteral("Begin a component Edge Refine stroke."));
  child.addBrushPoint(0.32, 0.50);
  child.addBrushPoint(0.76, 0.50);
  wait_until([&child, prior_revision] {
    const auto frame = EditorPreviewControllerTestAccess::liveBrushFrame(child);
    return child.activeMaskLiveBrushPreviewRevision() > prior_revision &&
        frame.point_count > 0 && frame.source_attempted && !frame.source.isNull();
  }, QStringLiteral("Wait for source-aware component brush live preview."));
  return EditorPreviewControllerTestAccess::liveBrushFrame(child);
}

void assert_component_source_and_live_coverage(
    const QImage& source, const BrushLivePreviewFrame& frame) {
  require(!source.isNull() && !frame.source.isNull() &&
              frame.source.size() == source.size() &&
              frame.raster.image.size() == frame.source.size(),
          QStringLiteral("Component source and brush matte must share the bounded full-canvas grid."));
  const auto sample = source.convertToFormat(QImage::Format_ARGB32);
  const auto live_source = frame.source.convertToFormat(QImage::Format_ARGB32);
  require(sample.pixelColor(5, 5).alpha() == 0 &&
              sample.pixelColor(38, 48).red() > sample.pixelColor(38, 48).blue() &&
              sample.pixelColor(88, 48).blue() > sample.pixelColor(88, 48).red(),
          QStringLiteral("Composed source must expose transparent, magenta and cyan pixels."));
  require(live_source.pixelColor(5, 5).alpha() == 0 &&
              std::abs(live_source.pixelColor(38, 48).red() - sample.pixelColor(38, 48).red()) <= 1 &&
              std::abs(live_source.pixelColor(88, 48).blue() - sample.pixelColor(88, 48).blue()) <= 1,
          QStringLiteral("Live Edge Refine must sample the rendered component RGB and alpha."));

  const auto alpha = frame.raster.alpha;
  const auto width = frame.raster.image.width();
  const auto height = frame.raster.image.height();
  require(alpha.size() == width * height,
          QStringLiteral("The live edge matte must match its composed source grid."));
  qint64 magenta_coverage = 0;
  qint64 cyan_coverage = 0;
  qint64 transparent_coverage = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto pixel = sample.pixelColor(x, y);
      const auto coverage = static_cast<unsigned char>(alpha.at(y * width + x));
      if (pixel.alpha() == 0) transparent_coverage += coverage;
      else if (pixel.red() > pixel.green() + 30 && pixel.blue() > pixel.green() + 30)
        magenta_coverage += coverage;
      else if (pixel.green() > pixel.red() + 30 && pixel.blue() > pixel.red() + 30)
        cyan_coverage += coverage;
    }
  }
  require(magenta_coverage > 0 && cyan_coverage * 10 < magenta_coverage &&
              transparent_coverage == 0,
          QStringLiteral("The edge matte must follow sampled magenta artwork and remain outside cyan and transparency."));
}

void assert_reopened_edge_matte_canonical_render(
    EditorPreviewController& child, const QImage& component, const QVariantMap& edge_mask) {
  const auto set_id = edge_mask.value(QStringLiteral("maskSetId")).toString();
  const auto alpha = edge_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto width = edge_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto height = edge_mask.value(QStringLiteral("segmentationHeight")).toInt();
  require(!set_id.isEmpty() && component.size() == QSize(width, height) &&
              alpha.size() == width * height,
          QStringLiteral("Reopened source-aware edge data must match the full-resolution component basis."));

  require(child.addAdjustmentLayer(QStringLiteral("Edge Matte Render Probe")) &&
              child.setPhotoLayerAdjustmentValue(0, QStringLiteral("exposure"), 1.2),
          QStringLiteral("Create a visible adjustment whose rendering is clipped by the reopened edge matte."));
  settle(child, QStringLiteral("Settle the full-resolution edge matte render probe."));
  auto masked = recipe(child);
  auto layer = masked.layers.first().toMap();
  layer.insert(QStringLiteral("selectionScoped"), true);
  layer.insert(QStringLiteral("selectionMaskSetId"), set_id);
  layer.insert(QStringLiteral("selectionMaskSetIds"), QVariantList{set_id});
  layer.insert(QStringLiteral("maskEnabled"), true);
  layer.insert(QStringLiteral("maskOpacity"), 1.0);
  layer.insert(QStringLiteral("maskDensity"), 1.0);
  masked.layers[0] = layer;
  // Keep the complete persisted set here. The Edge Refine brush is a child of
  // its source selection, and stripping that source makes this an invalid
  // reconstruction of the user's saved selection.

  auto unmasked = masked;
  layer = unmasked.layers.first().toMap();
  layer.insert(QStringLiteral("selectionScoped"), false);
  layer.insert(QStringLiteral("maskEnabled"), false);
  layer.remove(QStringLiteral("selectionMaskSetId"));
  layer.remove(QStringLiteral("selectionMaskSetIds"));
  unmasked.layers[0] = layer;

  auto neutral = masked;
  layer = neutral.layers.first().toMap();
  auto adjustments = layer.value(QStringLiteral("adjustments")).toMap();
  adjustments.insert(QStringLiteral("exposure"), 0.0);
  layer.insert(QStringLiteral("adjustments"), adjustments);
  layer.insert(QStringLiteral("exposure"), 0.0);
  neutral.layers[0] = layer;

  const auto rendered_neutral = render_component_state(neutral, component.size());
  const auto rendered_with_matte = render_component_state(masked, component.size());
  const auto rendered_without_matte = render_component_state(unmasked, component.size());
  const auto source = component.convertToFormat(QImage::Format_ARGB32);
  const auto baseline = rendered_neutral.convertToFormat(QImage::Format_ARGB32);
  const auto with_matte = rendered_with_matte.convertToFormat(QImage::Format_ARGB32);
  const auto without_matte = rendered_without_matte.convertToFormat(QImage::Format_ARGB32);
  qint64 magenta_masked_changed = 0;
  qint64 cyan_masked_changed = 0;
  qint64 magenta_unmasked_changed = 0;
  qint64 cyan_unmasked_changed = 0;
  qint64 magenta_masked_changed_pixels = 0;
  qint64 cyan_masked_changed_pixels = 0;
  qint64 magenta_unmasked_changed_pixels = 0;
  qint64 cyan_unmasked_changed_pixels = 0;
  qint64 magenta_expected = 0;
  qint64 cyan_expected = 0;
  qint64 nonzero_matte_pixels = 0;
  for (const auto coverage : alpha) {
    if (static_cast<unsigned char>(coverage) > 0) ++nonzero_matte_pixels;
  }
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const auto pixel = baseline.pixelColor(x, y);
      const auto coverage = static_cast<unsigned char>(alpha.at(y * width + x));
      if (pixel.alpha() == 0) {
        require(baseline.pixelColor(x, y).alpha() == 0 &&
                    with_matte.pixelColor(x, y).alpha() == baseline.pixelColor(x, y).alpha() &&
                    without_matte.pixelColor(x, y).alpha() == baseline.pixelColor(x, y).alpha(),
                QStringLiteral("Canonical mask rendering must preserve transparent component pixels."));
      } else if (pixel.red() > pixel.green() + 30 && pixel.blue() > pixel.green() + 30) {
        if (coverage >= 16) magenta_expected += coverage;
        const auto masked_delta = color_distance(with_matte.pixelColor(x, y), baseline.pixelColor(x, y));
        const auto unmasked_delta = color_distance(without_matte.pixelColor(x, y), baseline.pixelColor(x, y));
        magenta_masked_changed += masked_delta;
        magenta_unmasked_changed += unmasked_delta;
        if (masked_delta > 2) ++magenta_masked_changed_pixels;
        if (unmasked_delta > 2) ++magenta_unmasked_changed_pixels;
      } else if (pixel.green() > pixel.red() + 30 && pixel.blue() > pixel.red() + 30) {
        if (coverage >= 16) cyan_expected += coverage;
        const auto masked_delta = color_distance(with_matte.pixelColor(x, y), baseline.pixelColor(x, y));
        const auto unmasked_delta = color_distance(without_matte.pixelColor(x, y), baseline.pixelColor(x, y));
        cyan_masked_changed += masked_delta;
        cyan_unmasked_changed += unmasked_delta;
        if (masked_delta > 2) ++cyan_masked_changed_pixels;
        if (unmasked_delta > 2) ++cyan_unmasked_changed_pixels;
      }
    }
  }
  std::cout << "reopened_edge_render magenta_masked_changed_pixels=" << magenta_masked_changed_pixels
            << " cyan_masked_changed_pixels=" << cyan_masked_changed_pixels
            << " magenta_unmasked_changed_pixels=" << magenta_unmasked_changed_pixels
            << " cyan_unmasked_changed_pixels=" << cyan_unmasked_changed_pixels
            << " magenta_expected=" << magenta_expected
            << " cyan_expected=" << cyan_expected
            << " magenta_masked_changed=" << magenta_masked_changed
            << " cyan_masked_changed=" << cyan_masked_changed
            << " magenta_unmasked_changed=" << magenta_unmasked_changed
            << " cyan_unmasked_changed=" << cyan_unmasked_changed
            << " source_magenta=" << source.pixelColor(38, 48).name(QColor::HexArgb).toStdString()
            << " neutral_magenta=" << baseline.pixelColor(38, 48).name(QColor::HexArgb).toStdString()
            << " masked_magenta=" << with_matte.pixelColor(38, 48).name(QColor::HexArgb).toStdString()
            << " unmasked_magenta=" << without_matte.pixelColor(38, 48).name(QColor::HexArgb).toStdString()
            << " source_cyan=" << source.pixelColor(88, 48).name(QColor::HexArgb).toStdString()
            << " neutral_cyan=" << baseline.pixelColor(88, 48).name(QColor::HexArgb).toStdString()
            << " masked_cyan=" << with_matte.pixelColor(88, 48).name(QColor::HexArgb).toStdString()
            << " unmasked_cyan=" << without_matte.pixelColor(88, 48).name(QColor::HexArgb).toStdString()
            << " mask_id=" << edge_mask.value(QStringLiteral("id")).toString().toStdString()
            << " set_id=" << set_id.toStdString()
            << " op=" << edge_mask.value(QStringLiteral("operation")).toString().toStdString()
            << " opacity=" << edge_mask.value(QStringLiteral("opacity")).toDouble()
            << " enabled=" << edge_mask.value(QStringLiteral("enabled"), true).toBool()
            << '\n';
  if (magenta_expected <= 0 || magenta_masked_changed <= 0 || magenta_masked_changed_pixels <= 0 ||
      cyan_expected * 10 >= magenta_expected || cyan_masked_changed * 10 >= magenta_masked_changed ||
      magenta_unmasked_changed <= 0 || cyan_unmasked_changed <= 0) {
    const auto diagnostic_dir = qEnvironmentVariable("PHOTO_EDITOR_VISUAL_PROOF_DIR");
    if (!diagnostic_dir.isEmpty()) {
      static_cast<void>(QDir().mkpath(diagnostic_dir));
      static_cast<void>(with_matte.save(QDir(diagnostic_dir).filePath(QStringLiteral("component-edge-with-matte.png"))));
      static_cast<void>(without_matte.save(QDir(diagnostic_dir).filePath(QStringLiteral("component-edge-without-matte.png"))));
      static_cast<void>(baseline.save(QDir(diagnostic_dir).filePath(QStringLiteral("component-edge-neutral-baseline.png"))));
      static_cast<void>(source.save(QDir(diagnostic_dir).filePath(QStringLiteral("component-edge-source.png"))));
    }
  }
  QStringList persisted_sources;
  for (const auto& value : child.maskStack()) {
    const auto source_mask = value.toMap();
    persisted_sources.append(QStringLiteral("%1{%2,parent=%3,op=%4,bytes=%5}")
        .arg(source_mask.value(QStringLiteral("id")).toString(),
             source_mask.value(QStringLiteral("kind")).toString(),
             source_mask.value(QStringLiteral("parentId")).toString(),
             source_mask.value(QStringLiteral("operation")).toString())
        .arg(source_mask.value(QStringLiteral("segmentationData")).toByteArray().size()));
  }
  const auto failure = QStringLiteral(
      "Reopened source-aware coverage must clip canonical full-resolution rendering to sampled magenta artwork. "
      "expected{magenta=%1,cyan=%2} maskedDelta{magenta=%3/%4,cyan=%5/%6} "
      "unmaskedDelta{magenta=%7/%8,cyan=%9/%10} matte{nonzero=%11,alphaMagenta=%12,alphaCyan=%13} "
      "pixels{sourceMagenta=%14,neutralMagenta=%15,maskedMagenta=%16,unmaskedMagenta=%17,"
      "sourceCyan=%18,neutralCyan=%19,maskedCyan=%20,unmaskedCyan=%21} "
      "mask{id=%22,set=%23,parent=%24,operation=%25,enabled=%26,auto=%27,protect=%28,fineEdge=%29,contrast=%30} "
      "persistedSet=[%31]")
      .arg(magenta_expected).arg(cyan_expected)
      .arg(magenta_masked_changed).arg(magenta_masked_changed_pixels)
      .arg(cyan_masked_changed).arg(cyan_masked_changed_pixels)
      .arg(magenta_unmasked_changed).arg(magenta_unmasked_changed_pixels)
      .arg(cyan_unmasked_changed).arg(cyan_unmasked_changed_pixels)
      .arg(nonzero_matte_pixels)
      .arg(static_cast<unsigned char>(alpha.at(48 * width + 38)))
      .arg(static_cast<unsigned char>(alpha.at(48 * width + 88)))
      .arg(source.pixelColor(38, 48).name(QColor::HexArgb),
           baseline.pixelColor(38, 48).name(QColor::HexArgb),
           with_matte.pixelColor(38, 48).name(QColor::HexArgb),
           without_matte.pixelColor(38, 48).name(QColor::HexArgb),
           source.pixelColor(88, 48).name(QColor::HexArgb),
           baseline.pixelColor(88, 48).name(QColor::HexArgb),
           with_matte.pixelColor(88, 48).name(QColor::HexArgb),
           without_matte.pixelColor(88, 48).name(QColor::HexArgb))
      .arg(edge_mask.value(QStringLiteral("id")).toString(), set_id,
           edge_mask.value(QStringLiteral("parentId")).toString(),
           edge_mask.value(QStringLiteral("operation")).toString())
      .arg(edge_mask.value(QStringLiteral("enabled"), true).toBool())
      .arg(edge_mask.value(QStringLiteral("brushAutoMask")).toBool())
      .arg(edge_mask.value(QStringLiteral("brushProtectEdges")).toBool())
      .arg(edge_mask.value(QStringLiteral("selectionFineEdge")).toDouble())
      .arg(edge_mask.value(QStringLiteral("selectionEdgeContrast")).toDouble())
      .arg(persisted_sources.join(QStringLiteral(",")));
  require(magenta_expected > 0 && magenta_masked_changed > 0 && magenta_masked_changed_pixels > 0 &&
              cyan_expected * 10 < magenta_expected &&
              cyan_masked_changed * 10 < magenta_masked_changed &&
              magenta_unmasked_changed > 0 && cyan_unmasked_changed > 0 &&
              magenta_unmasked_changed_pixels > 0 && cyan_unmasked_changed_pixels > 0,
          failure);
}

void run_component_edge_refinement_workflow(const QString& runtime) {
  constexpr int width = 128;
  constexpr int height = 96;
  QImage photo(width, height, QImage::Format_RGB32);
  photo.fill(QColor(QStringLiteral("#52657d")));
  const auto photo_path = runtime + QStringLiteral("/parent.png");
  require(photo.save(photo_path, "PNG"), QStringLiteral("Save the parent photo fixture."));

  EditorPreviewController parent;
  parent.loadAsset(QStringLiteral("component-edge-refine-parent"), photo_path,
                   {}, {}, {}, {}, {});
  settle(parent, QStringLiteral("Load the parent photo."));
  require(parent.addFillLayer(QStringLiteral("#777777"), QStringLiteral("Component seed")),
          QStringLiteral("Create a layer to convert into a component."));
  settle(parent, QStringLiteral("Settle the source layer before conversion."));
  const auto seed_id = recipe(parent).layers.first().toMap().value(QStringLiteral("id")).toString();
  require(parent.convertPhotoLayerToComponent(layer_index(recipe(parent), seed_id)),
          QStringLiteral("Convert the seed layer to an editable component."));
  settle(parent, QStringLiteral("Settle component conversion."));
  const auto component_id = component_layer_id(recipe(parent));
  require(!component_id.isEmpty(), QStringLiteral("Find the converted component document."));

  PhotoComponentWorkspaceController workspace(&parent);
  auto* child = open_child(workspace, parent, component_id);
  add_component_artwork(*child);
  require(workspace.apply(), QStringLiteral("Apply baseline component artwork."));
  wait_until([&workspace] { return !workspace.busy() && !workspace.active(); },
             QStringLiteral("Wait for baseline component Apply."));
  settle(parent, QStringLiteral("Settle the parent after baseline artwork Apply."));
  const auto parent_after_artwork = parent.currentAssetBoundAdjustmentPatchJson();
  const auto artwork_history = parent.editHistoryCurrentIndex();

  child = open_child(workspace, parent, component_id);
  auto* editor = child;
  prepare_edge_refine(*editor);
  const auto cancelled_frame = preview_edge_refine(*editor);
  const auto source_before_cancel = component_source(*editor);
  require(!cancelled_frame.source.isNull() &&
              cancelled_frame.source.size() == source_before_cancel.size() &&
              cancelled_frame.source.pixelColor(5, 5).alpha() ==
                  source_before_cancel.pixelColor(5, 5).alpha(),
          QStringLiteral("The cancellable preview must already hold component source pixels."));
  require(workspace.cancel(), QStringLiteral("Cancel the in-flight component refinement."));
  wait_until([&workspace] { return !workspace.busy() && !workspace.active(); },
             QStringLiteral("Wait for component Cancel."));
  require(parent.currentAssetBoundAdjustmentPatchJson() == parent_after_artwork &&
              parent.editHistoryCurrentIndex() == artwork_history,
          QStringLiteral("Cancel must discard the source-sampled draft and stale live preview."));

  child = open_child(workspace, parent, component_id);
  require(edge_refine_mask(*child).isEmpty(),
          QStringLiteral("Reopening after Cancel must not retain the cancelled edge brush."));
  const auto source = component_source(*child);
  require(source.size() == QSize(width, height) && source.pixelColor(5, 5).alpha() == 0,
          QStringLiteral("Reopened component must retain its authored artwork and transparent exterior."));
  prepare_edge_refine(*child);
  const auto live_frame = preview_edge_refine(*child);
  assert_component_source_and_live_coverage(source, live_frame);
  const auto live_alpha = live_frame.raster.alpha;
  const auto edge_brush_id = edge_refine_mask(*child).value(QStringLiteral("id")).toString();

  child->endActiveBrushStroke();
  settle(*child, QStringLiteral("Wait for source-sampled component brush commit."));
  const auto committed_brush = edge_refine_mask(*child);
  require(committed_brush.value(QStringLiteral("id")).toString() == edge_brush_id &&
              committed_brush.value(QStringLiteral("segmentationWidth")).toInt() == live_frame.raster.image.width() &&
              committed_brush.value(QStringLiteral("segmentationHeight")).toInt() == live_frame.raster.image.height() &&
              committed_brush.value(QStringLiteral("segmentationData")).toByteArray() == live_alpha,
          QStringLiteral("Committed component matte must exactly match the live preview grid and coverage."));

  require(workspace.apply(), QStringLiteral("Apply the source-sampled component refinement."));
  wait_until([&workspace] { return !workspace.busy() && !workspace.active(); },
             QStringLiteral("Wait for source-sampled component Apply."));
  settle(parent, QStringLiteral("Settle the parent after edge refinement Apply."));
  child = open_child(workspace, parent, component_id);
  const auto reopened_brush = edge_refine_mask(*child);
  require(reopened_brush.value(QStringLiteral("id")).toString() == edge_brush_id &&
              reopened_brush.value(QStringLiteral("segmentationData")).toByteArray() == live_alpha &&
              reopened_brush.value(QStringLiteral("segmentationWidth")).toInt() == live_frame.raster.image.width() &&
              reopened_brush.value(QStringLiteral("segmentationHeight")).toInt() == live_frame.raster.image.height() &&
              brush_history_uses_source_sampling(reopened_brush),
          QStringLiteral("Saved and reopened component Edge Refine must preserve the committed source-grid matte."));
  assert_reopened_edge_matte_canonical_render(*child, component_source(*child), reopened_brush);
  require(workspace.cancel(), QStringLiteral("Close the verification child without another edit."));
  wait_until([&workspace] { return !workspace.busy() && !workspace.active(); },
             QStringLiteral("Close the verification child."));
  QThreadPool::globalInstance()->waitForDone(60000);

  std::cout << "component_edge_refinement_workflow_test passed\n";
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", QByteArrayLiteral("1"));
  QGuiApplication app(argc, argv);
  try {
    run_component_edge_refinement_workflow(runtime.path());
  } catch (const std::exception& error) {
    std::cerr << "component_edge_refinement_workflow_test failed: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
