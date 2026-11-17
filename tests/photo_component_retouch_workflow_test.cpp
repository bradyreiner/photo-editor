#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "editor_preview_image_sink.h"
#include "photo_component_workspace_controller.h"
#include "photo_render_utils.h"
#include "desktop_pipeline_state_compiler.h"
#include "edit_recipe_pipeline_bridge.h"

#include <QBuffer>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QSaveFile>
#include <QThread>
#include <QThreadPool>
#include <QUuid>
#include <QUrl>
#include <QHash>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace {

constexpr int kExportAllowedChannelDelta = 2;

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

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

EditRecipeState recipe(const EditorPreviewController& editor) {
  const auto parsed = parse_adjustment_patch_json(
      editor.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "The retouch workflow recipe must parse.");
  return *parsed.state;
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    static_cast<std::size_t>(a.width()) * 4) != 0) return false;
  }
  return true;
}

int changed_pixel_count(const QImage& left, const QImage& right) {
  if (left.isNull() || right.isNull() || left.size() != right.size()) return -1;
  const auto a = left.convertToFormat(QImage::Format_RGBA8888);
  const auto b = right.convertToFormat(QImage::Format_RGBA8888);
  auto changed = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width(); ++x) {
      if (a.pixel(x, y) != b.pixel(x, y)) ++changed;
    }
  }
  return changed;
}

QVariantMap opaque_parent_sibling_overlay(int canvas_width, int canvas_height) {
  constexpr int x = 88;
  constexpr int y = 32;
  constexpr int width = 27;
  constexpr int height = 28;
  QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  image.fill(QColor(QStringLiteral("#259b58")));
  QByteArray png;
  QBuffer buffer(&png);
  require(buffer.open(QIODevice::WriteOnly) && image.save(&buffer, "PNG"),
          "Encode the unrelated opaque parent sibling overlay.");
  return QVariantMap{
      {QStringLiteral("id"), QStringLiteral("unrelated-parent-green-overlay")},
      {QStringLiteral("kind"), QStringLiteral("pixel")},
      {QStringLiteral("name"), QStringLiteral("Unrelated Green Sibling")},
      {QStringLiteral("pixelData"), png},
      {QStringLiteral("pixelWidth"), width},
      {QStringLiteral("pixelHeight"), height},
      {QStringLiteral("canvasWidth"), canvas_width},
      {QStringLiteral("canvasHeight"), canvas_height},
      {QStringLiteral("x"), x},
      {QStringLiteral("y"), y},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
      {QStringLiteral("scale"), 1.0},
      {QStringLiteral("rotation"), 0.0}};
}

QImage render(const QImage& source, const EditRecipeState& state) {
  const auto image = render_output_image_with_backend(
      source, state, QColorSpace(QColorSpace::SRgb), QImage::Format_ARGB32,
      RenderBackendPreference::CpuOnly);
  require(!image.isNull(), "The canonical retouch workflow render must be available.");
  return image;
}

template <typename Predicate>
void wait_until(Predicate done, const char* message, int timeout_ms = 120000) {
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
        !editor.brushStrokeActive() && !editor.selectionWorkBusy() &&
        !editor.photoExportBusy() && !editor.inpaintProgressActive();
  }, message);
}

void wait_workspace_closed(PhotoComponentWorkspaceController& workspace,
                           const char* message) {
  wait_until([&workspace]() { return !workspace.busy() && !workspace.active(); },
             message);
}

void wait_component_apply_closed(PhotoComponentWorkspaceController& workspace,
                                 EditorPreviewController& parent,
                                 EditorPreviewController& child) {
  QElapsedTimer timer;
  timer.start();
  while (workspace.active() && workspace.busy() && timer.elapsed() < 120000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  if (workspace.active()) {
    const auto detail = QStringLiteral(
        "Component Apply did not close: busy=%1 workspaceError=%2 parentStatus=%3 childStatus=%4 "
        "parentProcessing=%5 parentFullRender=%6 parentToolsBusy=%7 parentPreviewFailure=%8/%9 "
        "childProcessing=%10 childFullRender=%11 childToolsBusy=%12 childPreviewFailure=%13/%14")
        .arg(workspace.busy())
        .arg(workspace.error())
        .arg(parent.status())
        .arg(child.status())
        .arg(parent.processing())
        .arg(parent.fullRenderRunning())
        .arg(parent.professionalToolsBusy())
        .arg(parent.previewFailureStage())
        .arg(parent.previewFailureDetail())
        .arg(child.processing())
        .arg(child.fullRenderRunning())
        .arg(child.professionalToolsBusy())
        .arg(child.previewFailureStage())
        .arg(child.previewFailureDetail());
    std::cerr << detail.toStdString() << '\n';
    throw std::runtime_error(detail.toStdString());
  }
}

QVariantMap layer_by_id(const QVariantList& layers, const QString& id) {
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("id")).toString() == id) return layer;
  }
  for (const auto& value : layers) {
    const auto layer = value.toMap();
    for (const auto& key : {QStringLiteral("children"), QStringLiteral("layers")}) {
      const auto nested = layer_by_id(layer.value(key).toList(), id);
      if (!nested.isEmpty()) return nested;
    }
  }
  return {};
}

QVariantMap layer_by_id(const EditRecipeState& state, const QString& id) {
  return layer_by_id(state.layers, id);
}

QString top_layer_id(const EditRecipeState& state, const QString& kind,
                     const QString& name) {
  for (const auto& value : state.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() == kind &&
        layer.value(QStringLiteral("name")).toString() == name) {
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

EditRecipeState state_with_component_masks(const EditRecipeState& state,
                                          const QString& component_id,
                                          const QVariantList& masks) {
  auto result = state;
  const auto index = layer_index(result, component_id);
  require(index >= 0, "The derived render must retain its component instance.");
  auto layer = result.layers.at(index).toMap();
  auto document = layer.value(QStringLiteral("smartObjectDocument")).toMap();
  require(!document.isEmpty(), "The derived render component document must be available.");
  document.insert(QStringLiteral("maskStack"), masks);
  layer.insert(QStringLiteral("smartObjectDocument"), document);
  result.layers[index] = layer;
  return result;
}

QVariantMap active_mask(const EditorPreviewController& editor) {
  const auto masks = editor.maskStack();
  const auto index = editor.activeMaskIndex();
  if (index < 0 || index >= masks.size()) return {};
  return masks.at(index).toMap();
}

QImage provider_preview(const TestImageSink& sink, const QString& source) {
  const QUrl url(source);
  if (url.scheme() != QStringLiteral("image") ||
      url.host() != QStringLiteral("editor-preview")) return {};
  auto key = url.path();
  if (key.startsWith(QLatin1Char('/'))) key.remove(0, 1);
  return sink.image(key);
}

QString color_detail(const QColor& color) {
  return QStringLiteral("%1,%2,%3,%4")
      .arg(color.red()).arg(color.green()).arg(color.blue()).arg(color.alpha());
}

void brush_stroke(EditorPreviewController& editor, double x, double y,
                  double size) {
  editor.setActiveMaskBrushSize(size);
  editor.setActiveMaskBrushFlow(1.0);
  editor.beginActiveBrushStroke();
  require(editor.brushStrokeActive(), "The component repair brush must start a stroke.");
  editor.addBrushPoint(x, y);
  editor.endActiveBrushStroke();
  settle(editor, "The component repair stroke raster must commit before Apply.");
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
             "The parent history restore must finish.");
  QObject::disconnect(connection);
  require(succeeded, "The parent history restore must succeed.");
  settle(editor, "The parent render must settle after history restore.");
}

bool is_red(const QColor& color) {
  return color.alpha() > 100 && color.red() > color.green() + 45 &&
      color.red() > color.blue() + 35;
}

void assert_export_matches(const QString& path, const QImage& expected) {
  const QImage exported(path);
  require(!exported.isNull() && exported.size() == expected.size(),
          "The saved component export must use the parent canvas dimensions.");
  const auto actual = exported.convertToFormat(QImage::Format_RGBA8888);
  const auto reference = expected.convertToFormat(QImage::Format_RGBA8888);
  auto max_channel_delta = 0;
  auto max_delta_x = -1;
  auto max_delta_y = -1;
  auto first_bad_x = -1;
  auto first_bad_y = -1;
  QRgb first_bad_actual = 0;
  QRgb first_bad_expected = 0;
  for (int y = 0; y < actual.height(); ++y) {
    for (int x = 0; x < actual.width(); ++x) {
      const auto a = actual.pixel(x, y);
      const auto e = reference.pixel(x, y);
      const auto delta = std::max({qAbs(qRed(a) - qRed(e)),
                                   qAbs(qGreen(a) - qGreen(e)),
                                   qAbs(qBlue(a) - qBlue(e)),
                                   qAbs(qAlpha(a) - qAlpha(e))});
      if (delta > max_channel_delta) {
        max_channel_delta = delta;
        max_delta_x = x;
        max_delta_y = y;
      }
      if (delta > kExportAllowedChannelDelta && first_bad_x < 0) {
        first_bad_x = x;
        first_bad_y = y;
        first_bad_actual = a;
        first_bad_expected = e;
      }
    }
  }
  if (first_bad_x >= 0) {
    const auto detail = QStringLiteral(
        "Export parity mismatch at (%1,%2): actual=%3 expected=%4; maxChannelDelta=%5 at (%6,%7); "
        "export=%8/%9, reference=CPU/Final/DisplayReferred/SRGB/ARGB32, state.outputColorSpace=%10")
        .arg(first_bad_x).arg(first_bad_y)
        .arg(color_detail(QColor::fromRgba(first_bad_actual)))
        .arg(color_detail(QColor::fromRgba(first_bad_expected)))
        .arg(max_channel_delta).arg(max_delta_x).arg(max_delta_y)
        .arg(static_cast<int>(exported.format()))
        .arg(exported.colorSpace().description())
        .arg(expected.colorSpace().description());
    std::cerr << detail.toStdString() << '\n';
    require(false, "The exported component retouch must match its canonical render.");
  }
}

struct Receipt final {
  QString asset_id;
  QString source_path;
  QString export_path;
  QString canonical_patch;
  QString proof_directory;
  QImage source;
  QImage child_before;
  QImage after_clone;
  QImage after_patch;
  QImage final_child;
  QImage before;
  QImage after;
  QString heal_status;
  QString heal_model;
  QString heal_provider;
  int target_before_alpha = -1;
  int target_after_clone_alpha = -1;
  int patch_target_before_alpha = -1;
  int target_after_patch_alpha = -1;
  int heal_target_before_alpha = -1;
  int target_after_heal_alpha = -1;
  bool cancel_preserved_child = false;
  bool alpha_preserved = false;
  bool source_and_outside_unchanged = false;
  bool apply_one_history_step = false;
  bool undo_restored = false;
  bool redo_restored = false;
  bool reopen_pixel_identical = false;
  bool export_pixel_identical = false;
  int export_allowed_channel_delta = kExportAllowedChannelDelta;
};

void save_proof_image(const QString& proof_directory, const QString& file_name,
                      const QImage& image) {
  if (proof_directory.isEmpty()) return;
  require(!image.isNull(), "Retained component proof images must be available.");
  const auto path = QDir(proof_directory).filePath(file_name);
  require(image.save(path, "PNG"), "Write retained component retouch proof image.");
}

void write_proof_receipt(const Receipt& receipt) {
  if (receipt.proof_directory.isEmpty()) return;
  QJsonObject artifacts;
  artifacts.insert(QStringLiteral("before"), QStringLiteral("before.png"));
  artifacts.insert(QStringLiteral("source"), QStringLiteral("source.png"));
  artifacts.insert(QStringLiteral("canceledHealBefore"), QStringLiteral("cancel-before.png"));
  artifacts.insert(QStringLiteral("canceledHealAfter"), QStringLiteral("cancel-after.png"));
  artifacts.insert(QStringLiteral("clone"), QStringLiteral("clone.png"));
  artifacts.insert(QStringLiteral("patch"), QStringLiteral("patch.png"));
  artifacts.insert(QStringLiteral("heal"), QStringLiteral("heal.png"));
  artifacts.insert(QStringLiteral("appliedParent"), QStringLiteral("applied-parent.png"));
  artifacts.insert(QStringLiteral("reopened"), QStringLiteral("reopened.png"));
  artifacts.insert(QStringLiteral("export"), QStringLiteral("export.png"));

  QJsonObject summary;
  summary.insert(QStringLiteral("result"), QStringLiteral("passed"));
  summary.insert(QStringLiteral("canvasWidth"), receipt.final_child.width());
  summary.insert(QStringLiteral("canvasHeight"), receipt.final_child.height());
  summary.insert(QStringLiteral("healStatus"), receipt.heal_status);
  summary.insert(QStringLiteral("healModel"), receipt.heal_model);
  summary.insert(QStringLiteral("healRuntimeProvider"), receipt.heal_provider);
  summary.insert(QStringLiteral("partialTargetAlphaBefore"), receipt.target_before_alpha);
  summary.insert(QStringLiteral("partialTargetAlphaAfterClone"), receipt.target_after_clone_alpha);
  summary.insert(QStringLiteral("partialPatchTargetAlphaBefore"), receipt.patch_target_before_alpha);
  summary.insert(QStringLiteral("partialTargetAlphaAfterPatch"), receipt.target_after_patch_alpha);
  summary.insert(QStringLiteral("partialHealTargetAlphaBefore"), receipt.heal_target_before_alpha);
  summary.insert(QStringLiteral("partialTargetAlphaAfterHeal"), receipt.target_after_heal_alpha);
  summary.insert(QStringLiteral("canceledHealPreservedPixels"), receipt.cancel_preserved_child);
  summary.insert(QStringLiteral("retouchPreservedAlpha"), receipt.alpha_preserved);
  summary.insert(QStringLiteral("sourcesAndOutsideCoverageUnchanged"), receipt.source_and_outside_unchanged);
  summary.insert(QStringLiteral("applyCreatedOneHistoryStep"), receipt.apply_one_history_step);
  summary.insert(QStringLiteral("undoRestoredExactPixels"), receipt.undo_restored);
  summary.insert(QStringLiteral("redoRestoredExactPixels"), receipt.redo_restored);
  summary.insert(QStringLiteral("reopenRestoredExactPixels"), receipt.reopen_pixel_identical);
  summary.insert(QStringLiteral("exportMatchesCanonicalPixels"), receipt.export_pixel_identical);
  summary.insert(QStringLiteral("exportAllowedChannelDelta"), receipt.export_allowed_channel_delta);
  summary.insert(QStringLiteral("artifacts"), artifacts);

  QSaveFile output(QDir(receipt.proof_directory).filePath(QStringLiteral("receipt.json")));
  require(output.open(QIODevice::WriteOnly), "Open retained component retouch receipt.");
  const auto bytes = QJsonDocument(summary).toJson(QJsonDocument::Indented);
  require(output.write(bytes) == bytes.size() && output.commit(),
          "Write retained component retouch receipt.");
}

Receipt exercise_component_retouch(const QString& runtime_path,
                                   const QString& proof_directory) {
  constexpr int width = 128;
  constexpr int height = 96;

  QImage source(width, height, QImage::Format_RGB32);
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      source.setPixelColor(x, y, QColor(26 + x / 6, 38 + y / 5, 60 + (x + y) % 28));
    }
  }
  const auto source_path = QDir(runtime_path).filePath(QStringLiteral("retouch-parent.png"));
  require(source.save(source_path, "PNG"), "Write the parent photo fixture.");
  save_proof_image(proof_directory, QStringLiteral("source.png"), source);

  QImage transparent(width, height, QImage::Format_ARGB32_Premultiplied);
  transparent.setColorSpace(QColorSpace(QColorSpace::SRgb));
  transparent.fill(Qt::transparent);
  const auto transparent_path = QDir(runtime_path).filePath(QStringLiteral("retouch-child-source.png"));
  require(transparent.save(transparent_path, "PNG"), "Write the transparent component source fixture.");

  TestImageSink sink;
  EditorPreviewController root;
  root.setImageProvider(&sink);
  const auto root_asset_id = QStringLiteral("component-retouch-workflow-root");
  root.loadAsset(root_asset_id, source_path, {}, {}, {}, {}, {});
  settle(root, "The parent source preview must settle before creating the component.");
  require(root.addPhotoShape(QStringLiteral("rectangle"), 9, 20, 47, 62,
              width, height, QStringLiteral("#e22d24"), QStringLiteral("#e22d24"), 0.0) &&
          root.addPhotoShape(QStringLiteral("rectangle"), 79, 20, 118, 62,
              width, height, QStringLiteral("#259b58"), QStringLiteral("#259b58"), 0.0),
          "Create visible red and green editable artwork for the repair source.");
  settle(root, "The child artwork must render before grouping.");

  auto before_group = recipe(root);
  const auto red_id = top_layer_id(before_group, QStringLiteral("pixel"), QStringLiteral("Rectangle"));
  // Both rectangles share the same display name. Resolve their IDs by their authored x positions.
  QString left_id;
  QString right_id;
  for (const auto& value : before_group.layers) {
    const auto layer = value.toMap();
    if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("pixel") ||
        layer.value(QStringLiteral("name")).toString() != QStringLiteral("Rectangle")) continue;
    if (layer.value(QStringLiteral("x")).toInt() < width / 2) left_id = layer.value(QStringLiteral("id")).toString();
    else right_id = layer.value(QStringLiteral("id")).toString();
  }
  require(!red_id.isEmpty() && !left_id.isEmpty() && !right_id.isEmpty() && left_id != right_id,
          "The component fixture must expose two separate colored layers.");
  require(root.setPhotoLayerOpacity(layer_index(before_group, right_id), 0.8),
          "The green child layer must retain partial alpha for nondestructive retouch checks.");
  settle(root, "The partially transparent green layer must render before grouping.");
  before_group = recipe(root);
  const QVariantList members{layer_index(before_group, left_id), layer_index(before_group, right_id)};
  require(members.at(0).toInt() >= 0 && members.at(1).toInt() >= 0 &&
              root.groupPhotoLayers(members, QStringLiteral("Editable repair source")),
          "Group the red and green source artwork into one editable component.");
  settle(root, "The grouped source artwork must render before conversion.");
  auto grouped = recipe(root);
  const auto component_id = top_layer_id(grouped, QStringLiteral("group"), QStringLiteral("Editable repair source"));
  const auto component_index = layer_index(grouped, component_id);
  require(component_index >= 0 && root.convertPhotoLayerToComponent(component_index),
          "Convert the grouped artwork into an editable component document.");
  settle(root, "The converted component preview must settle.");
  const auto before_parent_state = recipe(root);
  const auto before_parent = render(source, before_parent_state);
  const auto before_parent_history = root.editHistoryCurrentIndex();

  PhotoComponentWorkspaceController workspace(&root);
  require(workspace.open(layer_index(before_parent_state, component_id)),
          "Open the component for isolated repair authoring.");
  wait_until([&workspace, &root]() {
    return !workspace.busy() && workspace.active() && workspace.controller() != &root;
  }, "The isolated component editor must open.");
  auto* child = workspace.controller();
  require(child != nullptr && child->componentRetouchAvailable() &&
              child->componentRetouchCapabilityReason().isEmpty(),
          "A valid component canvas must advertise source-aware retouch capability.");
  settle(*child, "The component child canvas must settle before repairs.");
  const auto child_width = child->sourceImageWidth();
  const auto child_height = child->sourceImageHeight();
  require(child_width == width && child_height == height,
          "The child document must keep the captured 128 by 96 source basis.");
  const auto child_source = render(transparent, recipe(*child));
  save_proof_image(proof_directory, QStringLiteral("cancel-before.png"), child_source);
  require(is_red(child_source.pixelColor(27, 40)) &&
              child_source.pixelColor(96, 40).green() > child_source.pixelColor(96, 40).red() + 30,
          "The transparent child source must contain the visible red and green authored layers.");

  // Cancel a final-resolution worker in flight, then cancel the nested draft.
  child->startHealBrush();
  require(child->activeMaskRetouchMode() == QStringLiteral("heal"),
          "Heal Brush must be available in component contents.");
  brush_stroke(*child, 106.0 / (width - 1), 40.0 / (height - 1), 0.045);
  const auto heal_draft = active_mask(*child);
  require(heal_draft.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1 &&
              heal_draft.value(QStringLiteral("componentRetouchCanvasWidth")).toInt() == width &&
              heal_draft.value(QStringLiteral("componentRetouchCanvasHeight")).toInt() == height,
          "A component Heal repair must persist its versioned captured-child source basis.");
  child->applyActiveRetouch();
  child->cancelActiveRetouch();
  settle(*child, "The canceled component inpaint worker must drain without publishing.");
  const auto canceled_heal = active_mask(*child);
  require(!canceled_heal.value(QStringLiteral("committed")).toBool() &&
              canceled_heal.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty(),
          "Canceling an in-flight child inpaint must keep its draft uncommitted and preserve its selection.");
  const auto canceled_child = render(transparent, recipe(*child));
  save_proof_image(proof_directory, QStringLiteral("cancel-after.png"), canceled_child);
  const auto cancel_preserved_child = same_pixels(canceled_child, child_source);
  require(cancel_preserved_child,
          "Canceling an in-flight child inpaint must leave the authored child pixels unchanged.");
  require(recipe(root) == before_parent_state && root.editHistoryCurrentIndex() == before_parent_history &&
              same_pixels(render(source, recipe(root)), before_parent),
          "Clone authoring must remain isolated from the parent until Apply.");
  require(workspace.cancel(), "Cancel the isolated Clone draft.");
  wait_workspace_closed(workspace, "Cancel must close the nested component workspace.");
  require(recipe(root) == before_parent_state && root.editHistoryCurrentIndex() == before_parent_history &&
              same_pixels(render(source, recipe(root)), before_parent),
          "Cancel must discard all child repairs without changing parent history or pixels.");

  require(workspace.open(layer_index(recipe(root), component_id)),
          "Reopen the component to author repairs that will be applied.");
  wait_until([&workspace, &root]() {
    return !workspace.busy() && workspace.active() && workspace.controller() != &root;
  }, "The component workspace must reopen after Cancel.");
  child = workspace.controller();
  require(child != nullptr && child->componentRetouchAvailable(),
          "The reopened child must retain its valid source-capture capability.");
  settle(*child, "The reopened component child must settle.");
  const auto child_before = render(transparent, recipe(*child));
  save_proof_image(proof_directory, QStringLiteral("before.png"), child_before);
  const auto target_before_alpha = child_before.pixelColor(96, 40).alpha();
  const auto patch_target_before_alpha = child_before.pixelColor(96, 51).alpha();
  const auto heal_target_before_alpha = child_before.pixelColor(106, 40).alpha();
  require(target_before_alpha > 0 && target_before_alpha < 255 &&
              patch_target_before_alpha > 0 && patch_target_before_alpha < 255 &&
              heal_target_before_alpha > 0 && heal_target_before_alpha < 255,
          "The green repair target must have partial alpha before Clone, Patch, and Heal.");

  child->startCloneBrush();
  child->setActiveMaskCloneSource(27.0 / (width - 1), 40.0 / (height - 1));
  brush_stroke(*child, 96.0 / (width - 1), 40.0 / (height - 1), 0.08);
  child->applyActiveRetouch();
  settle(*child, "The component Clone repair must commit.");
  const auto clone_state = recipe(*child);
  const auto committed_clone = active_mask(*child);
  QVariantMap serialized_clone;
  for (const auto& value : clone_state.mask_stack) {
    const auto candidate = value.toMap();
    if (candidate.value(QStringLiteral("id")).toString() ==
        committed_clone.value(QStringLiteral("id")).toString()) {
      serialized_clone = candidate;
      break;
    }
  }
  require(serialized_clone.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() == 1,
          "Adjustment-patch serialization must retain the component post-composite repair marker.");
  const auto after_clone = render(transparent, clone_state);
  save_proof_image(proof_directory, QStringLiteral("clone.png"), after_clone);
  const auto target_after_clone_alpha = after_clone.pixelColor(96, 40).alpha();
  if (!is_red(after_clone.pixelColor(96, 40)) ||
      after_clone.pixelColor(96, 40) == child_before.pixelColor(96, 40)) {
    const auto& mask = committed_clone;
    const auto& child_state = clone_state;
    const auto& recipe_mask = serialized_clone;
    const auto compiled = compile_desktop_pipeline_state(
        child_state, nullptr, compile_color_pipeline_config(child_state),
        DesktopRenderPurpose::Output, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(width, height), {},
        DesktopSourceGeometry{QSize(width, height), QRect(0, 0, width, height)},
        &transparent);
    const auto provider = provider_preview(sink, child->previewSource());
    const auto matte = mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto matte_width = mask.value(QStringLiteral("segmentationWidth")).toInt();
    const auto matte_height = mask.value(QStringLiteral("segmentationHeight")).toInt();
    const auto target_index = matte_width > 96 && matte_height > 40
        ? 40 * matte_width + 96 : -1;
    auto matte_nonzero = 0;
    for (const auto value : matte) if (static_cast<unsigned char>(value) != 0) ++matte_nonzero;
    const auto target_coverage = target_index >= 0 && target_index < matte.size()
        ? static_cast<unsigned char>(matte.at(target_index)) : 0;
    QStringList compiled_details;
    const auto describe_compiled = [&](const char* name,
                                       const std::vector<photo_editor::pipeline::LocalMask>& masks) {
      for (std::size_t index = 0; index < masks.size(); ++index) {
        const auto& item = masks[index];
        compiled_details.append(QStringLiteral("%1[%2]{kind=%3,op=%4,retouch=%5,enabled=%6,sourceSet=%7,points=%8,strokes=%9,matte=%10x%11,atTarget=%12}")
            .arg(QString::fromLatin1(name)).arg(index)
            .arg(QString::fromStdString(item.kind)).arg(QString::fromStdString(item.operation))
            .arg(QString::fromStdString(item.retouch_mode)).arg(item.enabled)
            .arg(item.clone_source_set).arg(item.brush_points.size()).arg(item.clone_strokes.size())
            .arg(item.segmentation_width).arg(item.segmentation_height)
            .arg(item.segmentation_mask.size() > static_cast<std::size_t>(target_index) && target_index >= 0
                     ? static_cast<int>(item.segmentation_mask[static_cast<std::size_t>(target_index)]) : -1));
        if (!item.brush_points.empty()) {
          const auto& point = item.brush_points.front();
          compiled_details.append(QStringLiteral("  point=(%1,%2 size=%3 erase=%4)")
              .arg(point.x, 0, 'f', 4).arg(point.y, 0, 'f', 4)
              .arg(point.size, 0, 'f', 4).arg(point.erase));
        }
        for (std::size_t stroke_index = 0; stroke_index < item.clone_strokes.size(); ++stroke_index) {
          const auto& stroke = item.clone_strokes[stroke_index];
          compiled_details.append(QStringLiteral("  stroke[%1]=(source=%2,%3 offset=%4,%5 points=%6 circle=%7)")
              .arg(stroke_index).arg(stroke.source_x, 0, 'f', 4).arg(stroke.source_y, 0, 'f', 4)
              .arg(stroke.offset_x, 0, 'f', 4).arg(stroke.offset_y, 0, 'f', 4)
              .arg(stroke.points.size()).arg(stroke.circular));
        }
      }
    };
    describe_compiled("pre", compiled.retouch_masks);
    describe_compiled("post", compiled.post_composite_retouch_masks);
    const auto brush_point_values = mask.value(QStringLiteral("brushPoints")).toList();
    const auto stroke_values = mask.value(QStringLiteral("brushStrokes")).toList();
    QStringList authored_details;
    for (int index = 0; index < brush_point_values.size(); ++index) {
      const auto point = brush_point_values.at(index).toMap();
      authored_details.append(QStringLiteral("authoredPoint[%1]=(%2,%3 size=%4 erase=%5)")
          .arg(index).arg(point.value(QStringLiteral("x")).toDouble(), 0, 'f', 4)
          .arg(point.value(QStringLiteral("y")).toDouble(), 0, 'f', 4)
          .arg(point.value(QStringLiteral("size")).toDouble(), 0, 'f', 4)
          .arg(point.value(QStringLiteral("erase")).toBool()));
    }
    for (int index = 0; index < stroke_values.size(); ++index) {
      const auto stroke = stroke_values.at(index).toMap();
      const auto settings = stroke.value(QStringLiteral("settings")).toMap();
      authored_details.append(QStringLiteral("authoredStroke[%1]=samples:%2 source:%3,%4 offset:%5,%6 raster:%7")
          .arg(index).arg(stroke.value(QStringLiteral("samples")).toList().size())
          .arg(settings.value(QStringLiteral("cloneSourceX")).toDouble(), 0, 'f', 4)
          .arg(settings.value(QStringLiteral("cloneSourceY")).toDouble(), 0, 'f', 4)
          .arg(settings.value(QStringLiteral("cloneOffsetX")).toDouble(), 0, 'f', 4)
          .arg(settings.value(QStringLiteral("cloneOffsetY")).toDouble(), 0, 'f', 4)
          .arg(settings.value(QStringLiteral("rasterVersion")).toInt()));
    }
    const auto detail = QStringLiteral(
        "Clone diagnostics: child source=%1 targetBefore=%2 targetAfterCPU=%3 targetProvider=%4 "
        "preview=%5 mode=%6 committed=%7 cloneSourceSet=%8 cloneSource=(%9,%10) offset=(%11,%12) "
        "points=%13 strokes=%14 segmentation=%15x%16/%17 nonzero=%18 targetCoverage=%19 marker=%20 "
        "recipeMarker=%21 recipeRole=%22 enabled=%23 kind=%24 operation=%25\n%26")
        .arg(color_detail(child_before.pixelColor(27, 40)))
        .arg(color_detail(child_before.pixelColor(96, 40)))
        .arg(color_detail(after_clone.pixelColor(96, 40)))
        .arg(provider.isNull() ? QStringLiteral("unavailable") : color_detail(provider.pixelColor(96, 40)))
        .arg(child->previewSource())
        .arg(mask.value(QStringLiteral("retouchMode")).toString())
        .arg(mask.value(QStringLiteral("committed")).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(mask.value(QStringLiteral("cloneSourceSet")).toBool() ? QStringLiteral("true") : QStringLiteral("false"))
        .arg(mask.value(QStringLiteral("cloneSourceX")).toDouble(), 0, 'f', 3)
        .arg(mask.value(QStringLiteral("cloneSourceY")).toDouble(), 0, 'f', 3)
        .arg(mask.value(QStringLiteral("cloneOffsetX")).toDouble(), 0, 'f', 3)
        .arg(mask.value(QStringLiteral("cloneOffsetY")).toDouble(), 0, 'f', 3)
        .arg(mask.value(QStringLiteral("brushPoints")).toList().size())
        .arg(mask.value(QStringLiteral("brushStrokes")).toList().size())
        .arg(mask.value(QStringLiteral("segmentationWidth")).toInt())
        .arg(mask.value(QStringLiteral("segmentationHeight")).toInt())
        .arg(matte.size())
        .arg(matte_nonzero)
        .arg(static_cast<int>(target_coverage))
        .arg(mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt())
        .arg(recipe_mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt())
        .arg(recipe_mask.value(QStringLiteral("toolRole")).toString())
        .arg(recipe_mask.value(QStringLiteral("enabled")).toBool())
        .arg(recipe_mask.value(QStringLiteral("kind")).toString())
        .arg(recipe_mask.value(QStringLiteral("operation")).toString()) +
        QStringLiteral(" compiled pre=%1 post=%2\n%3")
            .arg(compiled.retouch_masks.size()).arg(compiled.post_composite_retouch_masks.size())
            .arg(compiled_details.join(QLatin1Char('\n'))) + QLatin1Char('\n') + authored_details.join(QLatin1Char('\n'));
    std::cerr << detail.toStdString() << '\n';
  }
  require(is_red(after_clone.pixelColor(96, 40)) &&
              after_clone.pixelColor(96, 40) != child_before.pixelColor(96, 40) &&
              target_after_clone_alpha == target_before_alpha,
          "Clone must sample red pixels from the composed child while preserving the target's partial alpha.");

  child->startPatchBrush();
  require(child->activeMaskRetouchMode() == QStringLiteral("patch") &&
              child->setActiveMaskPatchSourceRegion(0.06, 0.18, 0.30, 0.34),
          "Patch must allow choosing a source region from the composed child canvas.");
  brush_stroke(*child, 96.0 / (width - 1), 51.0 / (height - 1), 0.075);
  child->applyActiveRetouch();
  settle(*child, "The component Patch source-region job must finish.");
  const auto patch_mask = active_mask(*child);
  const auto patch_status = child->status();
  require(patch_mask.value(QStringLiteral("committed")).toBool() &&
              !patch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
              patch_status.contains(QStringLiteral("Source-region Patch baked")),
          "Patch must bake a source-region patch from actual child artwork.");
  const auto after_patch = render(transparent, recipe(*child));
  save_proof_image(proof_directory, QStringLiteral("patch.png"), after_patch);
  const auto patch_target_before = child_before.pixelColor(96, 51);
  const auto patch_target_after = after_patch.pixelColor(96, 51);
  const auto target_after_patch_alpha = after_patch.pixelColor(96, 51).alpha();
  const auto baked_patch = QImage::fromData(
      patch_mask.value(QStringLiteral("inpaintPatchData")).toByteArray(), "PNG");
  const auto baked_patch_source = baked_patch.isNull()
      ? QColor() : baked_patch.pixelColor(baked_patch.width() / 2, baked_patch.height() / 2);
  const auto patch_has_source_color = is_red(baked_patch_source);
  const auto patch_changes_target_with_color_match =
      patch_target_after.red() >= patch_target_before.red() + 40 &&
      patch_target_after.green() <= patch_target_before.green() - 5 &&
      patch_target_after.alpha() == patch_target_before.alpha() &&
      target_after_patch_alpha > 0 && target_after_patch_alpha < 255;
  if (!patch_has_source_color || !patch_changes_target_with_color_match) {
    const auto patch_recipe = recipe(*child);
    const auto compiled_patch = compile_desktop_pipeline_state(
        patch_recipe, nullptr, compile_color_pipeline_config(patch_recipe),
        DesktopRenderPurpose::Output, LocalMaskRasterPolicy::UseStoredRaster,
        QSize(width, height), {},
        DesktopSourceGeometry{QSize(width, height), QRect(0, 0, width, height)},
        &transparent);
    const auto patch_index = patch_mask.value(QStringLiteral("segmentationWidth")).toInt() > 96
        ? 51 * patch_mask.value(QStringLiteral("segmentationWidth")).toInt() + 96 : -1;
    const auto patch_matte = patch_mask.value(QStringLiteral("segmentationData")).toByteArray();
    const auto patch_coverage = patch_index >= 0 && patch_index < patch_matte.size()
        ? static_cast<unsigned char>(patch_matte.at(patch_index)) : 0;
    QStringList partition;
    const auto describe_patch_partition = [&](const char* label,
        const std::vector<photo_editor::pipeline::LocalMask>& masks) {
      for (const auto& compiled_mask : masks) {
        partition.append(QStringLiteral("%1 kind=%2 mode=%3 enabled=%4 remove=%5 patch=%6x%7 at=%8,%9 source=%10x%11")
            .arg(QString::fromLatin1(label)).arg(QString::fromStdString(compiled_mask.kind))
            .arg(QString::fromStdString(compiled_mask.retouch_mode)).arg(compiled_mask.enabled)
            .arg(compiled_mask.remove_strength, 0, 'f', 2).arg(compiled_mask.inpaint_patch_width)
            .arg(compiled_mask.inpaint_patch_height).arg(compiled_mask.inpaint_patch_x)
            .arg(compiled_mask.inpaint_patch_y).arg(compiled_mask.inpaint_source_width)
            .arg(compiled_mask.inpaint_source_height));
      }
    };
    describe_patch_partition("pre", compiled_patch.retouch_masks);
    describe_patch_partition("post", compiled_patch.post_composite_retouch_masks);
    std::cerr << QStringLiteral(
        "Patch diagnostics: targetBefore=%1 targetAfter=%2 sourceRegion=%3,%4 %5x%6 "
        "patchBounds=%7,%8 %9x%10 patchImage=%11x%12 patchCenter=%13 maskTarget=%14 "
        "marker=%15 recipeMarker=%16 status=%17 pre=%18 post=%19\n%20")
        .arg(color_detail(child_before.pixelColor(96, 51)))
        .arg(color_detail(after_patch.pixelColor(96, 51)))
        .arg(patch_mask.value(QStringLiteral("patchSourceX")).toDouble(), 0, 'f', 3)
        .arg(patch_mask.value(QStringLiteral("patchSourceY")).toDouble(), 0, 'f', 3)
        .arg(patch_mask.value(QStringLiteral("patchSourceWidth")).toDouble(), 0, 'f', 3)
        .arg(patch_mask.value(QStringLiteral("patchSourceHeight")).toDouble(), 0, 'f', 3)
        .arg(patch_mask.value(QStringLiteral("inpaintPatchX")).toInt())
        .arg(patch_mask.value(QStringLiteral("inpaintPatchY")).toInt())
        .arg(patch_mask.value(QStringLiteral("inpaintPatchWidth")).toInt())
        .arg(patch_mask.value(QStringLiteral("inpaintPatchHeight")).toInt())
        .arg(baked_patch.width()).arg(baked_patch.height())
        .arg(color_detail(baked_patch_source)).arg(static_cast<int>(patch_coverage))
        .arg(patch_mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt())
        .arg(patch_recipe.mask_stack.last().toMap()
                 .value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt())
        .arg(patch_status)
        .arg(compiled_patch.retouch_masks.size())
        .arg(compiled_patch.post_composite_retouch_masks.size())
        .arg(partition.join(QLatin1Char('\n'))).toStdString() << '\n';
  }
  require(patch_has_source_color && patch_changes_target_with_color_match,
          "Patch must sample the red child source and apply a visible seam-matched change to the green target.");

  child->startHealBrush();
  require(child->activeMaskRetouchMode() == QStringLiteral("heal"),
          "Heal Brush must be available in component contents.");
  brush_stroke(*child, 106.0 / (width - 1), 40.0 / (height - 1), 0.045);
  child->applyActiveRetouch();
  settle(*child, "The component Heal inpaint job must settle at final resolution.");
  const auto heal_mask = active_mask(*child);
  const auto heal_status = child->status();
  require(heal_mask.value(QStringLiteral("committed")).toBool() &&
              !heal_mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty() &&
              (heal_status.contains(QStringLiteral("LaMa Model Inpaint patch baked")) ||
               heal_status.contains(QStringLiteral("Local CPU Fill patch baked"))),
          "Heal must bake an honestly identified model or CPU fallback patch from the captured child.");
  const auto final_child = render(transparent, recipe(*child));
  save_proof_image(proof_directory, QStringLiteral("heal.png"), final_child);
  const auto target_after_heal_alpha = final_child.pixelColor(106, 40).alpha();
  const auto alpha_before = child_before.convertToFormat(QImage::Format_RGBA8888);
  const auto alpha_after = final_child.convertToFormat(QImage::Format_RGBA8888);
  auto alpha_preserved = true;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (qAlpha(alpha_before.pixel(x, y)) != qAlpha(alpha_after.pixel(x, y))) {
        alpha_preserved = false;
        break;
      }
    }
    if (!alpha_preserved) break;
  }
  require(alpha_preserved, "Component retouch must preserve every authored-layer alpha value.");
  require(target_after_clone_alpha == target_before_alpha &&
              target_after_patch_alpha == patch_target_before_alpha &&
              target_after_heal_alpha == heal_target_before_alpha,
          "Clone, Patch, and Heal must preserve the partially transparent target alpha exactly.");
  const auto source_and_outside_unchanged =
      final_child.pixelColor(27, 40) == child_before.pixelColor(27, 40) &&
      final_child.pixelColor(70, 80) == child_before.pixelColor(70, 80);
  require(source_and_outside_unchanged,
          "Repairs must leave their red source artwork and unrelated transparent child area unchanged.");
  require(recipe(root) == before_parent_state && root.editHistoryCurrentIndex() == before_parent_history &&
              same_pixels(render(source, recipe(root)), before_parent),
          "Clone, Patch and Heal must remain a private child draft before Apply.");

  const auto child_recipe = recipe(*child);
  std::cerr << "component_apply_begin childMasks=" << child_recipe.mask_stack.size()
            << " layers=" << child_recipe.layers.size() << '\n';
  const auto apply_requested = workspace.apply();
  std::cerr << "component_apply_return accepted=" << apply_requested
            << " busy=" << workspace.busy() << " active=" << workspace.active()
            << " error=" << workspace.error().toStdString()
            << " parentStatus=" << root.status().toStdString() << '\n';
  if (!apply_requested) {
    const auto detail = QStringLiteral("Component Apply was rejected: workspaceError=%1 parentStatus=%2")
        .arg(workspace.error(), root.status());
    throw std::runtime_error(detail.toStdString());
  }
  wait_component_apply_closed(workspace, root, *child);
  const auto after_parent_state = recipe(root);
  const auto after_parent = render(source, after_parent_state);
  save_proof_image(proof_directory, QStringLiteral("applied-parent.png"), after_parent);
  const auto persisted_layer = layer_by_id(after_parent_state, component_id);
  const auto persisted_document = persisted_layer.value(QStringLiteral("smartObjectDocument")).toMap();
  const auto persisted_masks = persisted_document.value(QStringLiteral("maskStack")).toList();
  QStringList persisted_component_repairs;
  auto persisted_component_marker_count = 0;
  for (const auto& value : persisted_masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() != 1) continue;
    ++persisted_component_marker_count;
    persisted_component_repairs.append(QStringLiteral("%1:%2:set=%3")
        .arg(mask.value(QStringLiteral("retouchMode")).toString())
        .arg(mask.value(QStringLiteral("repairId")).toString())
        .arg(mask.value(QStringLiteral("maskSetId")).toString()));
  }
  const auto applied_pixel_diff_count = changed_pixel_count(before_parent, after_parent);
  const auto apply_one_history_step =
      root.editHistoryCurrentIndex() == before_parent_history + 1 &&
      !same_pixels(after_parent, before_parent);
  std::cerr << QStringLiteral(
      "component_apply_result historyBefore=%1 historyAfter=%2 historyDelta=%3 "
      "beforeRevision=%4 afterRevision=%5 beforeLayerCount=%6 afterLayerCount=%7 "
      "beforeMaskCount=%8 afterMaskCount=%9 v1RetouchCount=%10 changedPixels=%11 "
      "identicalPixels=%12 repairManifest=%13\n")
      .arg(before_parent_history).arg(root.editHistoryCurrentIndex())
      .arg(root.editHistoryCurrentIndex() - before_parent_history)
      .arg(layer_by_id(before_parent_state, component_id)
               .value(QStringLiteral("smartObjectDocument")).toMap()
               .value(QStringLiteral("revision")).toInt())
      .arg(persisted_document.value(QStringLiteral("revision")).toInt())
      .arg(before_parent_state.layers.size()).arg(after_parent_state.layers.size())
      .arg(layer_by_id(before_parent_state, component_id)
               .value(QStringLiteral("smartObjectDocument")).toMap()
               .value(QStringLiteral("maskStack")).toList().size())
      .arg(persisted_masks.size()).arg(persisted_component_marker_count)
      .arg(applied_pixel_diff_count).arg(same_pixels(after_parent, before_parent))
      .arg(persisted_component_repairs.join(QLatin1Char(','))).toStdString() << '\n';
  require(apply_one_history_step,
          "Applying all component repairs must create one parent history step and visibly change output pixels.");

  auto parent_before_with_sibling = before_parent_state;
  auto parent_after_with_sibling = after_parent_state;
  const auto unrelated_sibling = opaque_parent_sibling_overlay(width, height);
  parent_before_with_sibling.layers.prepend(unrelated_sibling);
  parent_after_with_sibling.layers.prepend(unrelated_sibling);
  const auto sibling_before = render(source, parent_before_with_sibling);
  const auto sibling_after = render(source, parent_after_with_sibling);
  require(same_pixels(sibling_before, sibling_after),
          "Component repairs must stay inside their owner and leave an opaque unrelated parent sibling unchanged.");

  const auto compile_parent_at_policy = [&](LocalMaskRasterPolicy raster_policy,
                                            const QSize& output_size) {
    return compile_desktop_pipeline_state(
        after_parent_state, nullptr,
        compile_color_pipeline_config(after_parent_state),
        DesktopRenderPurpose::Output, raster_policy, output_size, {},
        DesktopSourceGeometry{QSize(width, height), QRect(0, 0, width, height)},
        &source);
  };
  const auto find_component_clone = [&](
      const photo_editor::pipeline::ColorAdjustmentState& compiled)
      -> const photo_editor::pipeline::LocalMask* {
    for (const auto& layer : compiled.photo_layers) {
      if (QString::fromStdString(layer.id) != component_id) continue;
      for (const auto& mask : layer.component_retouch_masks) {
        if (mask.retouch_mode == "clone") return &mask;
      }
    }
    return nullptr;
  };
  const auto stored_same_size = compile_parent_at_policy(
      LocalMaskRasterPolicy::UseStoredRaster, QSize(width, height));
  const auto rebuilt_same_size = compile_parent_at_policy(
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, QSize(width, height));
  const auto* stored_clone = find_component_clone(stored_same_size);
  const auto* rebuilt_clone = find_component_clone(rebuilt_same_size);
  require(stored_clone != nullptr && rebuilt_clone != nullptr &&
              stored_clone->segmentation_width == width &&
              stored_clone->segmentation_height == height &&
              stored_clone->segmentation_mask == rebuilt_clone->segmentation_mask,
          "Output-resolution compilation must preserve the accepted source-gated component Clone matte at its captured size.");
  const auto clone_matte_index = static_cast<std::size_t>(39 * width + 95);
  require(clone_matte_index < stored_clone->segmentation_mask.size() &&
              stored_clone->segmentation_mask[clone_matte_index] > 0,
          "The source-gated Clone matte must cover the export-difference diagnostic pixel.");

  const QSize doubled_size(width * 2, height * 2);
  const auto stored_doubled = compile_parent_at_policy(
      LocalMaskRasterPolicy::UseStoredRaster, doubled_size);
  const auto rebuilt_doubled = compile_parent_at_policy(
      LocalMaskRasterPolicy::RebuildEditableAtOutputResolution, doubled_size);
  const auto* stored_doubled_clone = find_component_clone(stored_doubled);
  const auto* rebuilt_doubled_clone = find_component_clone(rebuilt_doubled);
  require(stored_doubled_clone != nullptr && rebuilt_doubled_clone != nullptr &&
              stored_doubled_clone->segmentation_width == doubled_size.width() &&
              stored_doubled_clone->segmentation_height == doubled_size.height() &&
              stored_doubled_clone->segmentation_mask == rebuilt_doubled_clone->segmentation_mask,
          "Output resizing must resample the accepted component matte identically under stored and editable raster policies.");
  const auto doubled_clone_sample = static_cast<std::size_t>(78 * doubled_size.width() + 190);
  const auto doubled_clone_outside = static_cast<std::size_t>(160 * doubled_size.width() + 140);
  require(doubled_clone_sample < stored_doubled_clone->segmentation_mask.size() &&
              stored_doubled_clone->segmentation_mask[doubled_clone_sample] > 0 &&
              stored_doubled_clone->segmentation_mask[doubled_clone_sample] < 255 &&
              doubled_clone_outside < stored_doubled_clone->segmentation_mask.size() &&
              stored_doubled_clone->segmentation_mask[doubled_clone_outside] == 0,
          "Resizing must retain captured partial-alpha coverage without spreading the Clone matte outside its authored region.");

  auto unaccepted_masks = persisted_masks;
  auto unaccepted_clone = false;
  auto unaccepted_patch = false;
  auto unaccepted_heal = false;
  auto component_repair_count = 0;
  for (int index = 0; index < unaccepted_masks.size(); ++index) {
    auto mask = unaccepted_masks.at(index).toMap();
    if (mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() != 1)
      continue;
    ++component_repair_count;
    const auto mode = mask.value(QStringLiteral("retouchMode")).toString();
    if (mode == QStringLiteral("clone")) unaccepted_clone = true;
    if (mode == QStringLiteral("patch")) unaccepted_patch = true;
    if (mode == QStringLiteral("heal")) unaccepted_heal = true;
    mask.insert(QStringLiteral("committed"), false);
    mask.insert(QStringLiteral("inpaintPatchData"), QByteArray{});
    mask.insert(QStringLiteral("inpaintSourceMaskData"), QByteArray{});
    unaccepted_masks[index] = mask;
  }
  require(component_repair_count == 3 && unaccepted_clone && unaccepted_patch && unaccepted_heal,
          "The unaccepted-draft proof must cover the persisted Clone, Patch and Heal repairs.");
  const auto unaccepted_state = state_with_component_masks(
      after_parent_state, component_id, unaccepted_masks);
  require(same_pixels(render(source, unaccepted_state), before_parent),
          "Uncommitted component retouch drafts without accepted patch/source data must not change the parent render.");

  auto regenerating_masks = persisted_masks;
  auto regenerating_clone = false;
  auto regenerating_patch = false;
  auto regenerating_heal = false;
  for (int index = 0; index < regenerating_masks.size(); ++index) {
    auto mask = regenerating_masks.at(index).toMap();
    if (mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() != 1)
      continue;
    const auto mode = mask.value(QStringLiteral("retouchMode")).toString();
    if (mode == QStringLiteral("clone")) {
      regenerating_clone = mask.value(QStringLiteral("committed")).toBool();
      continue;
    }
    if (mode != QStringLiteral("patch") && mode != QStringLiteral("heal")) continue;
    const auto patch_data = mask.value(QStringLiteral("inpaintPatchData")).toByteArray();
    const auto accepted_matte = mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray();
    const auto accepted_width = mask.value(QStringLiteral("inpaintSourceMaskWidth")).toInt();
    const auto accepted_height = mask.value(QStringLiteral("inpaintSourceMaskHeight")).toInt();
    require(!patch_data.isEmpty() && accepted_width > 0 && accepted_height > 0 &&
                accepted_matte.size() == qint64(accepted_width) * accepted_height,
            "A regenerating Patch or Heal draft must retain its last accepted repair and source matte.");
    auto divergent_draft_matte = QByteArray(accepted_matte.size(), '\0');
    if (divergent_draft_matte == mask.value(QStringLiteral("segmentationData")).toByteArray()) {
      divergent_draft_matte.fill(static_cast<char>(0xff));
    }
    mask.insert(QStringLiteral("committed"), false);
    mask.insert(QStringLiteral("segmentationData"), divergent_draft_matte);
    mask.insert(QStringLiteral("segmentationWidth"), accepted_width);
    mask.insert(QStringLiteral("segmentationHeight"), accepted_height);
    mask.insert(QStringLiteral("brushPoints"), QVariantList{
        QVariantMap{{QStringLiteral("x"), 0.05}, {QStringLiteral("y"), 0.90},
                    {QStringLiteral("size"), 0.20}, {QStringLiteral("erase"), true}}});
    mask.insert(QStringLiteral("brushStrokes"), QVariantList{});
    require(mask.value(QStringLiteral("inpaintPatchData")).toByteArray() == patch_data &&
                mask.value(QStringLiteral("inpaintSourceMaskData")).toByteArray() == accepted_matte &&
                mask.value(QStringLiteral("segmentationData")).toByteArray() != accepted_matte,
            "A regenerating draft must preserve accepted bytes while diverging its live brush matte.");
    if (mode == QStringLiteral("patch")) regenerating_patch = true;
    else regenerating_heal = true;
    regenerating_masks[index] = mask;
  }
  require(regenerating_clone && regenerating_patch && regenerating_heal,
          "The accepted-snapshot proof must keep Clone committed and exercise Patch plus Heal drafts.");
  const auto regenerating_state = state_with_component_masks(
      after_parent_state, component_id, regenerating_masks);
  require(same_pixels(render(source, regenerating_state), after_parent),
          "Regenerating component Patch and Heal drafts must render their accepted snapshots, not their changed live brush mattes.");

  bool saw_clone = false;
  bool saw_patch = false;
  bool saw_heal = false;
  for (const auto& value : persisted_masks) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("componentPostCompositeRetouchVersion")).toInt() != 1 ||
        mask.value(QStringLiteral("componentRetouchCanvasWidth")).toInt() != width ||
        mask.value(QStringLiteral("componentRetouchCanvasHeight")).toInt() != height) continue;
    const auto mode = mask.value(QStringLiteral("retouchMode")).toString();
    if (mode == QStringLiteral("clone")) saw_clone = true;
    if (mode == QStringLiteral("patch") && !mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) saw_patch = true;
    if (mode == QStringLiteral("heal") && !mask.value(QStringLiteral("inpaintPatchData")).toByteArray().isEmpty()) saw_heal = true;
  }
  require(saw_clone && saw_patch && saw_heal,
          "Canonical Apply must persist each repair with its component source basis and baked patch data.");

  const auto canonical_patch = root.currentAssetBoundAdjustmentPatchJson();
  const auto decoded = parse_adjustment_patch_json(canonical_patch);
  require(decoded.ok() && decoded.state->layers == after_parent_state.layers,
          "Canonical component serialization must retain the complete repaired child document.");
  const auto export_path = QDir(runtime_path).filePath(QStringLiteral("component-retouch-export.png"));

  restore_history(root, false);
  const auto undo_restored = recipe(root).layers == before_parent_state.layers &&
      same_pixels(render(source, recipe(root)), before_parent);
  require(undo_restored,
          "Undo must restore the original component pixels exactly.");
  restore_history(root, true);
  const auto redo_restored = recipe(root).layers == after_parent_state.layers &&
      same_pixels(render(source, recipe(root)), after_parent);
  require(redo_restored,
          "Redo must restore the exact Clone, Patch and Heal component output.");

  Receipt receipt;
  receipt.asset_id = root_asset_id;
  receipt.source_path = source_path;
  receipt.export_path = export_path;
  receipt.canonical_patch = canonical_patch;
  receipt.proof_directory = proof_directory;
  receipt.source = source;
  receipt.child_before = child_before;
  receipt.after_clone = after_clone;
  receipt.after_patch = after_patch;
  receipt.final_child = final_child;
  receipt.before = before_parent;
  receipt.after = after_parent;
  receipt.heal_status = heal_status;
  receipt.heal_model = heal_mask.value(QStringLiteral("inpaintModelName")).toString();
  receipt.heal_provider = heal_mask.value(QStringLiteral("inpaintRuntimeProvider")).toString();
  receipt.target_before_alpha = target_before_alpha;
  receipt.target_after_clone_alpha = target_after_clone_alpha;
  receipt.patch_target_before_alpha = patch_target_before_alpha;
  receipt.target_after_patch_alpha = target_after_patch_alpha;
  receipt.heal_target_before_alpha = heal_target_before_alpha;
  receipt.target_after_heal_alpha = target_after_heal_alpha;
  receipt.cancel_preserved_child = cancel_preserved_child;
  receipt.alpha_preserved = alpha_preserved;
  receipt.source_and_outside_unchanged = source_and_outside_unchanged;
  receipt.apply_one_history_step = apply_one_history_step;
  receipt.undo_restored = undo_restored;
  receipt.redo_restored = redo_restored;
  return receipt;
}

void reopen_and_export(Receipt* receipt) {
  require(receipt != nullptr, "The component proof receipt must be writable.");
  TestImageSink sink;
  EditorPreviewController reopened;
  reopened.setImageProvider(&sink);
  reopened.loadAsset(receipt->asset_id, receipt->source_path, {}, {}, {}, {},
                     receipt->canonical_patch, true);
  settle(reopened, "The saved component retouch recipe must reopen.");
  const auto reopened_state = recipe(reopened);
  const auto reopened_render = render(receipt->source, reopened_state);
  save_proof_image(receipt->proof_directory, QStringLiteral("reopened.png"), reopened_render);
  receipt->reopen_pixel_identical = same_pixels(reopened_render, receipt->after);
  require(receipt->reopen_pixel_identical,
          "Reopening the saved document must preserve the repaired component pixels exactly.");
  require(reopened.startFlattenedPhotoExport(receipt->export_path),
          "Start a real flattened PNG export from the reopened repaired document.");
  settle(reopened, "The flattened retouch export must finish.");
  require(reopened.photoExportError().isEmpty(),
          "The flattened retouch export must finish without errors.");
  assert_export_matches(receipt->export_path, receipt->after);
  receipt->export_pixel_identical = true;
  if (!receipt->proof_directory.isEmpty()) {
    const auto exported = QImage(receipt->export_path);
    save_proof_image(receipt->proof_directory, QStringLiteral("export.png"), exported);
  }
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir runtime;
  if (!runtime.isValid()) return 1;
  const auto proof_directory =
      qEnvironmentVariable("PHOTO_EDITOR_COMPONENT_RETOUCH_PROOF_DIR").trimmed();
  if (!proof_directory.isEmpty() && !QDir().mkpath(proof_directory)) {
    std::cerr << "FAIL: unable to create the requested component retouch proof directory\n";
    return 1;
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath(QStringLiteral("app-data")).toUtf8());
  qputenv("PHOTO_EDITOR_CACHE_DIR", runtime.filePath(QStringLiteral("cache")).toUtf8());
  qunsetenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE");
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyComponentRetouchWorkflowTests"));
  QCoreApplication::setApplicationName(QStringLiteral("PhotoComponentRetouch-%1")
      .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
  QStandardPaths::setTestModeEnabled(true);
  QGuiApplication application(argc, argv);
  struct DrainWorkers final {
    ~DrainWorkers() { QThreadPool::globalInstance()->waitForDone(120000); }
  } drain_workers;
  try {
    auto receipt = exercise_component_retouch(runtime.path(), proof_directory);
    reopen_and_export(&receipt);
    write_proof_receipt(receipt);
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
  std::cout << "photo_component_retouch_workflow_test passed\n";
  return 0;
}
