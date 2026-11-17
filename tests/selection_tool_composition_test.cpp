#include "ai_model_runtime.h"
#include "canonical_composite_mask_alpha.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_paint_surface.h"
#include "desktop_source_geometry.h"
#include "photo_paint_coordinates.h"
#include "photo_render_utils.h"

#include <QCoreApplication>
#include <QBuffer>
#include <cmath>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJSEngine>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QThread>

#include <iostream>
#include <stdexcept>

using namespace photo_editor::desktop;

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess {
  static void preview(EditorPreviewController& controller, const QByteArray& alpha, int width, int height) {
    controller.smart_select_preview_mask_ = {
        {QStringLiteral("segmentationData"), alpha}, {QStringLiteral("segmentationWidth"), width},
        {QStringLiteral("segmentationHeight"), height}};
  }
};
}

namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void settle(EditorPreviewController& controller) {
  QElapsedTimer timer;
  timer.start();
  do {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  } while ((controller.processing() || controller.brushStrokeActive() || controller.photoPaintBusy()) && timer.elapsed() < 15000);
  require(!controller.processing() && !controller.brushStrokeActive() && !controller.photoPaintBusy(), "Selection work must settle");
}
EditRecipeState recipe(const EditorPreviewController& controller) {
  const auto parsed = parse_adjustment_patch_json(controller.currentAdjustmentPatchJson());
  require(parsed.ok(), "Recipe must decode");
  return *parsed.state;
}
EditRecipeState asset_bound_recipe(const EditorPreviewController& controller) {
  const auto parsed = parse_adjustment_patch_json(controller.currentAssetBoundAdjustmentPatchJson());
  require(parsed.ok(), "Asset-bound recipe must decode");
  return *parsed.state;
}

// Run the production workspace functions against the real controller. Only
// presentation dependencies are supplied by this small host; no model download
// or full desktop shell is needed to test tool routing and composed alpha.
void install_routes(QJSEngine& engine, EditorPreviewController& controller) {
  QQmlEngine::setObjectOwnership(&controller, QQmlEngine::CppOwnership);
  engine.globalObject().setProperty(QStringLiteral("editorPreview"), engine.newQObject(&controller));
  auto qt = engine.newObject();
  qt.setProperty(QStringLiteral("ShiftModifier"), static_cast<int>(Qt::ShiftModifier));
  qt.setProperty(QStringLiteral("AltModifier"), static_cast<int>(Qt::AltModifier));
  engine.globalObject().setProperty(QStringLiteral("Qt"), qt);
  auto result = engine.evaluate(QStringLiteral(R"JS(
    var root = {
      editorPreviewController: editorPreview, componentWorkspaceActive: false,
      pendingSelectionAction: '', page: 'edit', editTool: 'masks', editInspectorTab: 'adjust', maskCreationOperation: 'add',
      selectedEditTarget: {}, selectedPhotoLayerIndices: [], photoLayerRows: editorPreview.photoLayers,
      maskSetRows: editorPreview.maskSets,
      maskRefinementSessionActive: false,
      endMaskShapeEditTransaction: function() {},
      setActivePhotoLayerSelection: function(index) { root.activeLayerIndex = index; },
      currentPhotoLayerData: function() { return editorPreview.photoLayers[root.activeLayerIndex]; },
      normalizedActivePhotoLayerIndex: function() { return root.activeLayerIndex; },
      selectedEditTargetScope: function() { return String(root.selectedEditTarget.scope || 'layer'); },
      shortcutTextInputActive: function() { return false; },
      setSelectedEditTarget: function(scope, index, maskSet, sourceIndex) {
        root.selectedEditTarget = {scope: scope, layerIndex: index,
          maskSetId: maskSet && maskSet.id ? String(maskSet.id) : '', sourceIndex: sourceIndex};
      },
      activeMaskSetData: function() { return {}; },
      selectedEditTargetMaskSetId: function() { return ''; },
      selectedEditTargetLayerIndex: function() { return root.activeLayerIndex === undefined ? -1 : root.activeLayerIndex; },
      selectedLayerTargetScope: function(index) {
        return String((root.photoLayerRows[index] || {}).kind || '') === 'adjustment' ? 'adjustment-layer' : 'photo-layer';
      },
      resolveTextSession: function() { return true; }, beginEditToolSwitch: function() {},
      clearTransientToolStateFor: function() { root.clearTransientMaskTools(); },
      finishEditToolTransitionSoon: function() {}
    };
  )JS"));
  require(!result.isError(), "Route host must initialize");
  QFile workspace(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  require(workspace.open(QIODevice::ReadOnly), "Production workspace must open");
  const auto source = QString::fromUtf8(workspace.readAll());
  for (const auto* name : {"normalizedMaskCreationOperation", "beginMaskSelectionSession", "clearTransientMaskTools", "cancelCloneSourcePick",
                         "setMaskCreationOperation", "maskOperationForModifierState", "objectPickModeForModifierState",
                         "prepareWorkingSelectionTarget", "selectActiveSelectionAdjustmentLayer", "createMaskSource", "createGeneratedSelectionSource",
                         "startMaskBrushSource", "startPixelBrushSource", "maskToolKeepsActiveMask", "applyEditToolActivation",
                         "beginMaskShapeEditTransaction", "endMaskShapeEditTransaction", "startMaskPathSource",
                         "handleMaskToolMenuAction", "selectionShortcutContextActive", "runSelectionAction",
                         "queueSelectionAction", "invertActiveMaskFromShortcut", "adjustmentLayerIndexForMaskSet", "maskSetLinkedLayerIndex",
                         "photoLayerMaskSetIds", "photoLayerMaskSetNames", "normalizedPhotoLayerMaskSetValues", "photoLayerReferencesMaskSet",
                         "maskSetsForAdjustmentLayer"}) {
    const auto start = source.indexOf(QStringLiteral("    function %1(").arg(QString::fromLatin1(name)));
    const auto end = source.indexOf(QStringLiteral("\n    }"), start);
    require(start >= 0 && end > start, "Production route must be found");
    const auto function = source.mid(start, end + 6 - start).trimmed();
    result = engine.evaluate(QStringLiteral("root.%1 = %2;").arg(QString::fromLatin1(name), function));
    if (result.isError()) throw std::runtime_error(result.toString().toStdString());
  }
}
void run(QJSEngine& engine, const QString& code) {
  const auto result = engine.evaluate(code);
  if (result.isError()) throw std::runtime_error(result.toString().toStdString());
}
}

namespace {
void verify_layer_paint_workflow(QTemporaryDir& runtime, const QImage& source, const QString& path) {
  const int width = source.width(), height = source.height();
  EditorPreviewController seed;
  seed.loadAsset("layer-paint-seed", path, {}, {}, {}, {}, {});
  settle(seed);
  seed.addMask("object");
  auto base = recipe(seed);
  require(!base.mask_stack.isEmpty(), "Pixel-paint fixture needs a selection source");
  auto mask = base.mask_stack[0].toMap();
  mask.insert("kind", "subject");
  QByteArray coverage(width * height, '\0');
  for (int y = 0; y < height; ++y)
    for (int x = 25; x < 85; ++x) coverage[y * width + x] = char(x == 25 ? 128 : 255);
  mask.insert("segmentationData", coverage);
  mask.insert("segmentationWidth", width);
  mask.insert("segmentationHeight", height);
  mask.insert("feather", 0.0);
  base.mask_stack[0] = mask;
  QImage opaque(width, height, QImage::Format_ARGB32);
  opaque.fill(QColor("#f0b080"));
  const auto linked_path = runtime.filePath("linked-paint-target.png");
  require(opaque.save(linked_path), "Save linked target pixels");
  const auto png = [&] {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    require(opaque.save(&buffer, "PNG"), "Encode target pixels"); return bytes;
  }();
  for (bool source_layer : {false, true}) {
    for (bool cropped : {false, true}) {
      auto state = base;
      if (cropped) { state.crop_rect_x = 0.125; state.crop_rect_y = 0.1; state.crop_rect_width = 0.75; state.crop_rect_height = 0.8; }
      QVariantMap target{{"id", "target"}, {"kind", source_layer ? "source" : "pixel"},
          {"name", "Chosen layer"}, {"pixelWidth", width}, {"pixelHeight", height},
          {"canvasWidth", width}, {"canvasHeight", height}, {"x", 7}, {"y", 0},
          {"scale", 0.92}, {"rotation", 13.0}, {"enabled", true}, {"opacity", 0.73}};
      if (source_layer) target.insert("sourcePath", linked_path);
      else target.insert("pixelData", png);
      // Source-area geometry is deliberately frozen before changing the crop.
      target.insert("coordinateSpace", "source-area");
      target.insert("coordinateArea", QVariantMap{{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}});
      auto sibling = target; sibling.insert("id", "sibling"); sibling.insert("locked", true);
      state.layers = {target, sibling};
      EditorPreviewController editor;
      editor.loadAsset(QStringLiteral("clipped-%1-%2").arg(source_layer).arg(cropped), path, {}, {}, {}, {}, serialize_edit_recipe_json(state));
      settle(editor); editor.setActiveMaskIndex(0);
      const auto original = recipe(editor);
      if (source_layer && !cropped) {
        require(editor.beginPhotoPaintStroke(0, {{"erase", true}, {"size", 20}}), "Begin cancellable source-layer eraser");
        editor.addPhotoPaintPoint(45, 60); editor.finishPhotoPaintStroke(false); settle(editor);
        require(recipe(editor).layers == original.layers, "Cancelling must retain the linked source and original pixels");
        EditorPreviewController queued;
        queued.loadAsset("queued-source-strokes", path, {}, {}, {}, {}, serialize_edit_recipe_json(original));
        settle(queued); queued.setActiveMaskIndex(0);
        for (double x : {45.0, 65.0}) {
          require(queued.beginPhotoPaintStroke(0, {{"erase", true}, {"size", 12}, {"hardness", 1.0},
              {"pressureSize", false}}), "Queue consecutive strokes on a linked source layer");
          const auto mapping = photo_paint_layer_to_document(original.layers[0].toMap(), original.layers,
              {source.size(), QRect(QPoint(), source.size())});
          require(mapping.valid, "Map source-layer pointer coordinates");
          const auto point = mapping.local_to_document.map(QPointF(x + 0.5, 60.5));
          queued.addPhotoPaintPoint(point.x(), point.y()); queued.finishPhotoPaintStroke();
        }
        settle(queued);
        const auto queued_pixels = rasterize_photo_paint_layer(recipe(queued).layers[0].toMap());
        if (!queued.photoPaintError().isEmpty() || queued_pixels.isNull() || queued_pixels.pixelColor(45, 60).alpha() != 0 || queued_pixels.pixelColor(65, 60).alpha() != 0)
          std::cerr << "queued error=" << queued.photoPaintError().toStdString() << " alpha=" << queued_pixels.pixelColor(45, 60).alpha() << ',' << queued_pixels.pixelColor(65, 60).alpha() << '\n';
        require(queued.photoPaintError().isEmpty() && !queued_pixels.isNull() &&
            queued_pixels.pixelColor(45, 60).alpha() == 0 && queued_pixels.pixelColor(65, 60).alpha() == 0,
            "Queued source strokes must retain both edits when the layer becomes pixel-editable");
      }
      require(editor.beginPhotoPaintStroke(0, {{"erase", true}, {"size", 4096}, {"hardness", 1.0},
          {"pressureSize", false}, {"opacity", 1.0}, {"flow", 1.0}}), "Begin selection-clipped eraser on chosen content layer");
      editor.addPhotoPaintPoint(width / 2.0, height / 2.0);
      require(editor.finishPhotoPaintStroke(), "Commit selected-layer eraser");
      settle(editor);
      if (!editor.photoPaintError().isEmpty()) throw std::runtime_error(editor.photoPaintError().toStdString());
      const auto erased = recipe(editor);
      const auto pixels = rasterize_photo_paint_layer(erased.layers[0].toMap());
      require(!pixels.isNull() && erased.layers[1] == original.layers[1], "Only the chosen layer can change");
      require(erased.layers[0].toMap().value("id") == target.value("id") &&
          erased.layers[0].toMap().value("opacity") == target.value("opacity") &&
          erased.layers[0].toMap().value("x") == target.value("x"), "Materialization must preserve layer identity and transform");
      const auto crop = crop_rect_for_image(source.size(), original);
      const DesktopSourceGeometry geometry{source.size(), crop};
      const auto alpha = evaluate_canonical_composite_mask_alpha(source.copy(crop), original.mask_stack, 1.0,
          {1.0, 1.0, 1.0}, geometry.mask_area(), QSizeF(width - 1, height - 1));
      const auto transform = photo_paint_layer_to_document(erased.layers[0].toMap(), erased.layers, geometry);
      require(alpha.valid && transform.valid, "Resolve cropped selection and layer transform");
      for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const auto doc = transform.local_to_document.map(QPointF(x + 0.5, y + 0.5));
        const int dx = int(std::floor(doc.x())), dy = int(std::floor(doc.y()));
        const int selected = dx >= 0 && dy >= 0 && dx < alpha.size.width() && dy < alpha.size.height()
            ? static_cast<unsigned char>(alpha.alpha_mask[dy * alpha.size.width() + dx]) : 0;
        require(std::abs(pixels.pixelColor(x, y).alpha() - (255 - selected)) <= 1,
            "Eraser must preserve every outside pixel and honor soft selection coverage after crop/transform");
      }
      const auto original_render = render_output_image_with_backend(source, original, QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      const auto erased_render = render_output_image_with_backend(source, erased, QColorSpace(QColorSpace::SRgb),
          QImage::Format_ARGB32, RenderBackendPreference::CpuOnly);
      require(!original_render.isNull() && original_render.size() == erased_render.size(), "Render original and edited layer compositions");
      for (int y = 0; y < alpha.size.height(); ++y) for (int x = 0; x < alpha.size.width(); ++x) {
        bool near_selection = false;
        for (int ny = std::max(0, y - 3); ny <= std::min(alpha.size.height() - 1, y + 3); ++ny)
          for (int nx = std::max(0, x - 3); nx <= std::min(alpha.size.width() - 1, x + 3); ++nx)
            near_selection = near_selection || static_cast<unsigned char>(alpha.alpha_mask[ny * alpha.size.width() + nx]) != 0;
        if (near_selection) continue; // Transformed layer resampling antialiases the selected boundary.
        const auto a = original_render.pixelColor(x, y), b = erased_render.pixelColor(x, y);
        if (std::abs(a.red() - b.red()) > 1 || std::abs(a.green() - b.green()) > 1 || std::abs(a.blue() - b.blue()) > 1)
          std::cerr << "outside color source=" << source_layer << " crop=" << cropped << " at " << x << ',' << y
            << " original=" << a.name().toStdString() << " erased=" << b.name().toStdString() << '\n';
        require(std::abs(a.red() - b.red()) <= 1 && std::abs(a.green() - b.green()) <= 1 && std::abs(a.blue() - b.blue()) <= 1,
            "Materializing and erasing must preserve rendered color outside selection");
      }
      editor.undo(); require(recipe(editor).layers == original.layers, "Undo restores original pixel/source recipe");
      editor.redo(); require(recipe(editor).layers == erased.layers, "Redo restores exact edited tiles");
      require(editor.invertActiveSelection(), "Invert current selection for painting");
      require(editor.beginPhotoPaintStroke(0, {{"color", "#1688ff"}, {"size", 4096}, {"hardness", 1.0},
          {"pressureSize", false}, {"opacity", 1.0}, {"flow", 1.0}}), "Paint automatically clips to inverted selection");
      editor.addPhotoPaintPoint(width / 2.0, height / 2.0); editor.finishPhotoPaintStroke(); settle(editor);
      require(editor.photoPaintError().isEmpty(), "Inverted-selection paint must finish");
      const auto painted = recipe(editor);
      const auto painted_pixels = rasterize_photo_paint_layer(painted.layers[0].toMap());
      require(painted.layers[1] == original.layers[1], "Painting must preserve sibling layer");
      require(painted_pixels.pixelColor(45, 60).alpha() == 0 && painted_pixels.pixelColor(110, 60).blue() > 200,
          "Paint must remain outside the original selection after inversion");
      EditorPreviewController reopened;
      reopened.loadAsset("reopened-clipped-paint", path, {}, {}, {}, {}, serialize_edit_recipe_json(painted)); settle(reopened);
      require(rasterize_photo_paint_layer(recipe(reopened).layers[0].toMap()) == painted_pixels,
          "Reopen must preserve painted pixels exactly");
      editor.deselectWorkingSelection();
      require(!editor.hasActiveMask() && editor.maskCount() == 1, "Deselect clears working selection without deleting the saved mask");
      require(!editor.beginPhotoPaintStroke(1, {{"erase", true}}), "Locked sibling rejects erasing");
    }
  }
}
}

namespace {
void verify_object_intersection(const QImage& source, const QString& path) {
  EditorPreviewController editor;
  editor.loadAsset("object-intersection", path, {}, {}, {}, {}, {});
  settle(editor);
  editor.addMask("rectangle");
  editor.setActiveMaskFeather(0);
  editor.setActiveMaskRadiusX(0.38);
  editor.setActiveMaskRadiusY(0.38);
  settle(editor);
  const auto original = editor.maskStack();
  const auto identity = editor.workingSelectionSetId();
  const auto alpha = [&] {
    const auto value = evaluate_canonical_composite_mask_alpha(source, editor.workingSelectionMasks(), 1.0, {1, 1, 1});
    require(value.valid, "Object overlap must have canonical coverage");
    return value.alpha_mask;
  };
  auto expected = alpha();
  QJSEngine engine;
  install_routes(engine, editor);
  for (int pass = 0; pass < 2; ++pass) {
    const auto before = editor.maskStack();
    const auto before_alpha = alpha();
    run(engine, "root.setMaskCreationOperation('intersect'); root.createMaskSource('object');");
    require(engine.evaluate("root.maskCreationOperation").toString() == "intersect" &&
                engine.evaluate("root.objectPickMode").toString() == "selection-intersect",
            "Object overlap must retain the chosen operation when arming the tool");
    QByteArray picked(source.width() * source.height(), '\0');
    for (int y = 0; y < source.height(); ++y) {
      for (int x = 0; x < source.width(); ++x) {
        const bool inside = pass == 0 ? x < source.width() * 3 / 5 : y < source.height() / 2;
        if (inside) picked[y * source.width() + x] = char(255);
      }
    }
    EditorPreviewControllerTestAccess::preview(editor, picked, source.width(), source.height());
    require(editor.acceptSmartSelectPreview(engine.evaluate("root.objectPickMode").toString(), false) == "selection-ai" &&
                editor.finishSelectionEditSession(), "Object overlap must commit through the selection workflow");
    settle(editor);
    for (qsizetype index = 0; index < expected.size(); ++index)
      if (picked[index] == '\0') expected[index] = '\0';
    require(alpha() == expected && editor.maskStack().size() == before.size() + 1 &&
                editor.maskStack().first() == original.first() && editor.workingSelectionSetId() == identity &&
                editor.photoLayerCount() == 0,
            "Repeated Object overlap must intersect the combined result without unioning picks or replacing original sources");
    const auto after = editor.maskStack();
    editor.undo(); settle(editor);
    require(editor.maskStack() == before && alpha() == before_alpha, "Undo restores exact pre-overlap sources and coverage");
    editor.redo(); settle(editor);
    require(editor.maskStack() == after && alpha() == expected, "Redo restores exact Object overlap coverage");
  }
  const auto retained = editor.maskStack();
  run(engine, "root.setMaskCreationOperation('replace');");
  require(engine.evaluate("root.maskCreationOperation").toString() == "add" && editor.maskStack() == retained,
          "Legacy New mode must not replace an existing selection through the workspace route");
}

QVariantList masks_for_set(const QVariantList& stack, const QString& set_id) {
  QVariantList result;
  for (const auto& value : stack)
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id)
      result.append(value);
  return result;
}

void verify_selection_tools_retarget_to_working_selection(const QImage&,
                                                           const QString& path) {
  EditorPreviewController seed;
  seed.loadAsset(QStringLiteral("mask-target-routing-seed"), path, {}, {}, {}, {}, {});
  settle(seed);
  seed.addMask(QStringLiteral("rectangle"));
  seed.setActiveMaskFeather(0.0);
  seed.setActiveMaskRadiusX(0.28);
  seed.setActiveMaskRadiusY(0.3);
  settle(seed);
  const auto working_set_id = seed.workingSelectionSetId();
  require(!working_set_id.isEmpty(), "Target-routing fixture needs a working selection");
  require(seed.saveWorkingSelection(QStringLiteral("Attached selection")),
      "Create a separate selection to attach as a layer mask");
  settle(seed);
  const auto saved_selection_state = asset_bound_recipe(seed).selection_state;
  require(saved_selection_state.value(QStringLiteral("currentSetId")).toString() == working_set_id,
      "Saving a named selection must preserve the working selection identity in asset-bound state");
  require(seed.addPaintLayer(), "Create the pixel layer that will own a separate mask");
  settle(seed);

  auto state = asset_bound_recipe(seed);
  require(state.selection_state.value(QStringLiteral("currentSetId")).toString() == working_set_id,
      "Adding the owner layer must preserve the working selection identity");
  QString attached_set_id;
  for (const auto& value : state.mask_stack) {
    const auto id = value.toMap().value(QStringLiteral("maskSetId")).toString();
    if (!id.isEmpty() && id != working_set_id) {
      attached_set_id = id;
      break;
    }
  }
  require(!attached_set_id.isEmpty(), "Target-routing fixture needs a distinct attached mask set");
  require(!state.layers.isEmpty(), "Pixel layer owner must be present in the recipe");
  auto layer = state.layers.first().toMap();
  layer.insert(QStringLiteral("maskEnabled"), true);
  layer.insert(QStringLiteral("selectionScoped"), true);
  layer.insert(QStringLiteral("selectionMaskSetId"), attached_set_id);
  layer.insert(QStringLiteral("selectionMaskSetIds"), QVariantList{attached_set_id});
  layer.insert(QStringLiteral("selectionMaskSetName"), QStringLiteral("Attached selection"));
  layer.insert(QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Attached selection")});
  state.layers[0] = layer;
  const auto serialized = serialize_edit_recipe_json(state);
  const auto round_trip = parse_adjustment_payload_or_legacy_recipe(serialized);
  require(round_trip.ok() &&
      round_trip.state->selection_state.value(QStringLiteral("currentSetId")).toString() == working_set_id,
      "Full recipe serialization must preserve the working selection identity");

  struct RouteCase {
    const char* name;
    const char* js;
  };
  const RouteCase cases[] = {
      {"brush add", "root.startMaskBrushSource('add');"},
      {"brush subtract", "root.startMaskBrushSource('subtract');"},
      {"lasso", "root.startMaskPathSource('lasso');"},
      {"polygon", "root.startMaskPathSource('polygon');"},
      {"pixel add", "root.startPixelBrushSource('add');"},
      {"pixel subtract", "root.startPixelBrushSource('subtract');"},
  };
  for (const auto& route : cases) {
    EditorPreviewController editor;
    editor.loadAsset(QStringLiteral("mask-target-routing-%1").arg(QString::fromLatin1(route.name)),
        path, {}, {}, {}, {}, serialized);
    settle(editor);
    require(editor.workingSelectionSetId() == working_set_id,
        "Reopen must keep the chosen working selection identity");
    require(editor.selectLayerMask(0, attached_set_id) && editor.editingLayerMask(),
        "Select the separate attached mask as the initial target");
    const auto attached_before = masks_for_set(editor.maskStack(), attached_set_id);
    const auto attached_layer_before = recipe(editor).layers.first();
    require(!attached_before.isEmpty(), "Attached mask fixture must contain source components");

    QJSEngine engine;
    install_routes(engine, editor);
    run(engine, QStringLiteral("root.activeLayerIndex=0; root.selectedEditTarget={scope:'mask',layerIndex:0}; %1")
        .arg(QString::fromLatin1(route.js)));
    settle(editor);
    const auto after_start = recipe(editor);
    const auto active = editor.activeMaskIndex() >= 0 && editor.activeMaskIndex() < after_start.mask_stack.size()
        ? after_start.mask_stack[editor.activeMaskIndex()].toMap()
        : QVariantMap{};
    if (editor.workingSelectionSetId() != working_set_id || editor.editingLayerMask() ||
        active.value(QStringLiteral("maskSetId")).toString() != working_set_id ||
        masks_for_set(after_start.mask_stack, attached_set_id) != attached_before ||
        after_start.layers.first() != attached_layer_before) {
      std::cerr << "selection tool route=" << route.name
                << " working=" << editor.workingSelectionSetId().toStdString()
                << " active-set=" << active.value(QStringLiteral("maskSetId")).toString().toStdString()
                << " attached-before=" << attached_before.size()
                << " attached-after=" << masks_for_set(after_start.mask_stack, attached_set_id).size()
                << " editing-layer-mask=" << editor.editingLayerMask() << '\n';
    }
    require(editor.workingSelectionSetId() == working_set_id && !editor.editingLayerMask(),
        "Selection authoring must switch from the inspected layer mask to the working selection");
    require(active.value(QStringLiteral("maskSetId")).toString() == working_set_id,
        "The new selection source must be owned by the retained working selection");
    require(masks_for_set(after_start.mask_stack, attached_set_id) == attached_before &&
        after_start.layers.first() == attached_layer_before,
        "Selection authoring must leave the attached mask set and its layer link unchanged");

    run(engine, QStringLiteral("root.endMaskShapeEditTransaction();"));
    if (editor.selectionEditSessionActive())
      require(editor.finishSelectionEditSession(), "Finish routed selection authoring session");
    settle(editor);
    const auto finished = recipe(editor);
    require(editor.workingSelectionSetId() == working_set_id &&
        masks_for_set(finished.mask_stack, attached_set_id) == attached_before &&
        finished.layers.first() == attached_layer_before,
        "Finishing selection authoring must preserve the attached mask target");
  }
}

void verify_same_name_selection_mask_ownership(const QString& path) {
  EditorPreviewController editor;
  editor.loadAsset(QStringLiteral("same-name-mask-owner"), path, {}, {}, {}, {}, {});
  settle(editor);
  editor.addMask(QStringLiteral("rectangle"));
  editor.setActiveMaskName(QStringLiteral("Shared selection"));
  editor.setActiveMaskFeather(0.0);
  editor.setActiveMaskRadiusX(0.31);
  editor.setActiveMaskRadiusY(0.27);
  settle(editor);
  const auto original_id = editor.workingSelectionSetId();
  require(!original_id.isEmpty(), "Same-name ownership fixture needs a working selection");

  require(editor.addAdjustmentLayer(QStringLiteral("Copied selection owner")),
      "Create an adjustment layer for the independent same-name mask");
  settle(editor);
  require(editor.applyWorkingSelectionToLayer(0),
      "Attach an independent copy of the working selection to the adjustment layer");
  settle(editor);

  auto state = asset_bound_recipe(editor);
  require(state.selection_state.value(QStringLiteral("currentSetId")).toString() == original_id,
      "Attaching the independent mask must keep the original working selection active");
  require(!state.layers.isEmpty(), "Same-name ownership fixture needs its adjustment layer");
  const auto copied_owner = state.layers.first().toMap();
  const auto copied_ids = copied_owner.value(QStringLiteral("selectionMaskSetIds")).toList();
  require(copied_owner.value(QStringLiteral("kind")).toString() == QStringLiteral("adjustment") &&
              copied_owner.value(QStringLiteral("selectionScoped")).toBool() && copied_ids.size() == 1,
      "The adjustment layer must own exactly one copied selection set");
  const auto copied_id = copied_ids.first().toString();
  require(!copied_id.isEmpty() && copied_id != original_id &&
              copied_owner.value(QStringLiteral("selectionMaskSetName")).toString() ==
                  QStringLiteral("Shared selection"),
      "The copied set must have its own ID and the same display name as the working set");

  QJSEngine engine;
  install_routes(engine, editor);
  run(engine, QStringLiteral("root.photoLayerRows = editorPreview.photoLayers; root.maskSetRows = editorPreview.maskSets;"));
  run(engine, QStringLiteral(
      "if (root.adjustmentLayerIndexForMaskSet({id:'%1', name:'Shared selection'}) !== -1) "
      "throw new Error('An explicit working-set ID must not match a same-name independent owner');")
      .arg(original_id));
  const auto copied_owner_index = static_cast<int>(state.layers.size()) - 1;
  run(engine, QStringLiteral(
      "if (root.adjustmentLayerIndexForMaskSet({id:'%1', name:'Shared selection'}) !== %2) "
      "throw new Error('An explicit copied-set ID must resolve to its exact owner');")
      .arg(copied_id).arg(copied_owner_index));
  run(engine, QStringLiteral(
      "var linkedCopy = root.maskSetsForAdjustmentLayer(%1); "
      "if (linkedCopy.length !== 1 || String(linkedCopy[0].id) !== '%2') "
      "throw new Error('The adjustment layer list must contain only its canonical copied set');")
      .arg(copied_owner_index).arg(copied_id));

  const auto before_ensure_layer_count = editor.photoLayerCount();
  require(editor.activateWorkingSelection(), "Select the original working set before ensuring its owner");
  const auto original_owner_index = editor.ensureActiveSelectionAdjustmentLayer();
  settle(editor);
  require(original_owner_index >= 0 && editor.photoLayerCount() == before_ensure_layer_count + 1,
      "Ensuring an owner for the original set must add a distinct adjustment layer");
  auto after_ensure = asset_bound_recipe(editor);
  const auto original_owner = after_ensure.layers.at(original_owner_index).toMap();
  require(original_owner.value(QStringLiteral("selectionMaskSetId")).toString() == original_id &&
              original_owner.value(QStringLiteral("selectionMaskSetIds")).toList() == QVariantList{original_id},
      "The new adjustment owner must reference the original set by canonical ID");
  require(after_ensure.layers.at(original_owner_index + 1).toMap()
              .value(QStringLiteral("selectionMaskSetIds")).toList() == QVariantList{copied_id},
      "The same-name copied owner must remain attached to its independent set");
  run(engine, QStringLiteral("root.photoLayerRows = editorPreview.photoLayers; root.maskSetRows = editorPreview.maskSets;"));
  run(engine, QStringLiteral(
      "if (root.adjustmentLayerIndexForMaskSet({id:'%1', name:'Shared selection'}) !== %2) "
      "throw new Error('After creation the original ID must resolve to its distinct owner');")
      .arg(original_id).arg(original_owner_index));
  const auto stable_layer_count = editor.photoLayerCount();
  const auto repeated_owner_index = editor.ensureActiveSelectionAdjustmentLayer();
  settle(editor);
  require(repeated_owner_index == original_owner_index && editor.photoLayerCount() == stable_layer_count,
      "Repeated ensure must reuse the original selection owner instead of adding or reusing the same-name copy");

  auto legacy_state = state;
  QVariantMap legacy_owner;
  legacy_owner.insert(QStringLiteral("id"), QStringLiteral("legacy-name-only-owner"));
  legacy_owner.insert(QStringLiteral("name"), QStringLiteral("Legacy selection owner"));
  legacy_owner.insert(QStringLiteral("kind"), QStringLiteral("adjustment"));
  legacy_owner.insert(QStringLiteral("enabled"), true);
  legacy_owner.insert(QStringLiteral("selectionScoped"), true);
  legacy_owner.insert(QStringLiteral("selectionMaskSetName"), QStringLiteral("Shared selection"));
  legacy_owner.insert(QStringLiteral("selectionMaskSetNames"), QVariantList{QStringLiteral("Shared selection")});
  legacy_state.layers.append(legacy_owner);
  const auto legacy_recipe = serialize_edit_recipe_json(legacy_state);

  EditorPreviewController legacy_editor;
  legacy_editor.loadAsset(QStringLiteral("same-name-mask-legacy-owner"), path, {}, {}, {}, {}, legacy_recipe);
  settle(legacy_editor);
  require(legacy_editor.workingSelectionSetId() == original_id &&
              legacy_editor.photoLayerCount() == static_cast<int>(legacy_state.layers.size()),
      "A name-only legacy owner must survive loading beside the explicit-ID copied owner");
  const auto normalized_legacy_state = asset_bound_recipe(legacy_editor);
  const auto normalized_legacy_owner = normalized_legacy_state.layers.last().toMap();
  require(normalized_legacy_owner.value(QStringLiteral("selectionMaskSetIds")).toList().isEmpty() &&
              normalized_legacy_owner.value(QStringLiteral("selectionMaskSetNames")).toList() ==
                  QVariantList{QStringLiteral("Shared selection")},
      "The legacy fixture must retain its name-only reference after layer normalization");

  QJSEngine legacy_engine;
  install_routes(legacy_engine, legacy_editor);
  run(legacy_engine, QStringLiteral(
      "root.photoLayerRows = editorPreview.photoLayers; root.maskSetRows = editorPreview.maskSets;"));
  const auto legacy_owner_index = static_cast<int>(normalized_legacy_state.layers.size()) - 1;
  run(legacy_engine, QStringLiteral(
      "if (root.adjustmentLayerIndexForMaskSet({id:'%1', name:'Shared selection'}) !== %2) "
      "throw new Error('An explicit ID must continue through a same-name copied owner to the legacy name-only owner');")
      .arg(original_id).arg(legacy_owner_index));
  const auto legacy_layer_count = legacy_editor.photoLayerCount();
  require(legacy_editor.activateWorkingSelection(), "Select the working set before resolving its legacy owner");
  const auto legacy_ensure_index = legacy_editor.ensureActiveSelectionAdjustmentLayer();
  settle(legacy_editor);
  require(legacy_ensure_index == legacy_owner_index && legacy_editor.photoLayerCount() == legacy_layer_count,
      "C++ ownership lookup must preserve name-only legacy fallback alongside an explicit same-name copy");

  auto intersection_state = state;
  auto intersection_owner = intersection_state.layers.first().toMap();
  intersection_owner.insert(QStringLiteral("selectionIntersectionMaskSetNames"),
      QVariantList{QStringLiteral("Shared selection")});
  intersection_state.layers[0] = intersection_owner;
  EditorPreviewController intersection_editor;
  intersection_editor.loadAsset(QStringLiteral("same-name-mask-legacy-intersection"), path,
      {}, {}, {}, {}, serialize_edit_recipe_json(intersection_state));
  settle(intersection_editor);
  const auto intersection_layer_count = intersection_editor.photoLayerCount();
  require(intersection_editor.activateWorkingSelection(), "Select the working set before resolving its legacy intersection owner");
  const auto intersection_ensure_index = intersection_editor.ensureActiveSelectionAdjustmentLayer();
  settle(intersection_editor);
  require(intersection_ensure_index == 0 && intersection_editor.photoLayerCount() == intersection_layer_count,
      "A name-only legacy intersection must match even when the layer has a different explicit primary set ID");

  QJSEngine intersection_engine;
  install_routes(intersection_engine, intersection_editor);
  run(intersection_engine, QStringLiteral(
      "root.photoLayerRows = [{kind:'adjustment', selectionScoped:true, "
      "selectionMaskSetIds:['%1'], selectionMaskSetNames:['Shared selection'], "
      "selectionIntersectionMaskSetIds:['%2'], selectionIntersectionMaskSetNames:['Shared selection']}]; "
      "root.maskSetRows = [{id:'%1',name:'Shared selection'}, {id:'%2',name:'Shared selection'}, "
      "{id:'unlinked-same-name',name:'Shared selection'}]; "
      "var allIds = root.photoLayerMaskSetIds(root.photoLayerRows[0]); "
      "var allSets = root.maskSetsForAdjustmentLayer(0); "
      "if (allIds.join(',') !== '%1,%2' || allSets.length !== 2 || "
      "allSets[0].id !== '%1' || allSets[1].id !== '%2') "
      "throw new Error('Primary and intersection masks must appear once in their stored order'); "
      "if (root.maskSetLinkedLayerIndex({id:'%2',name:'Shared selection'}) !== 0 || "
      "root.adjustmentLayerIndexForMaskSet({id:'%2',name:'Shared selection'}) !== 0 || "
      "root.maskSetLinkedLayerIndex({id:'unlinked-same-name',name:'Shared selection'}) !== -1) "
      "throw new Error('Intersection ownership must use exact IDs without a same-name ghost');")
      .arg(copied_id, original_id));
}
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  try {
    constexpr int width = 160;
    constexpr int height = 120;
    QImage source(width, height, QImage::Format_RGB32);
    source.fill(QColor(QStringLiteral("#667788")));
    const auto path = runtime.filePath(QStringLiteral("photo.png"));
    require(source.save(path), "Fixture must save");
    EditorPreviewController controller;
    controller.loadAsset(QStringLiteral("fixture-seed"), path, {}, {}, {}, {}, {});
    settle(controller);
    // Seed deterministic Subject coverage without invoking a model backend.
    // The route tests below exercise composition, not segmentation quality.
    controller.addMask(QStringLiteral("object"));
    auto state = recipe(controller);
    require(!state.mask_stack.isEmpty(), "Subject composition fixture needs a selection source");
    auto subject = state.mask_stack[0].toMap();
    subject.insert(QStringLiteral("kind"), QStringLiteral("subject"));
    QByteArray subject_alpha(width * height, '\0');
    for (int y = 25; y < 95; ++y)
      for (int x = 25; x < 75; ++x) subject_alpha[y * width + x] = char(255);
    subject.insert(QStringLiteral("segmentationData"), subject_alpha);
    subject.insert(QStringLiteral("segmentationWidth"), width);
    subject.insert(QStringLiteral("segmentationHeight"), height);
    subject.insert(QStringLiteral("feather"), 0.0);
    state.mask_stack[0] = subject;
    controller.loadAsset(QStringLiteral("subject-composition"), path, {}, {}, {}, {}, serialize_edit_recipe_json(state));
    settle(controller);
    controller.setActiveMaskIndex(0);
    const auto original = controller.maskStack()[0];
    const auto set_id = original.toMap().value(QStringLiteral("maskSetId"));
    QJSEngine engine;
    install_routes(engine, controller);
    const auto alpha_at = [&](int x, int y) {
      const auto alpha = evaluate_canonical_composite_mask_alpha(source, controller.maskStack(), 1.0, {1.0, 1.0, 1.0});
      require(alpha.valid, "Composed selection must evaluate");
      return static_cast<unsigned char>(alpha.alpha_mask[y * width + x]);
    };
    if (alpha_at(45, 60) <= 240 || alpha_at(125, 60) >= 10)
      std::cerr << "subject alpha=" << int(alpha_at(45, 60)) << " outside=" << int(alpha_at(125, 60))
                << " bytes=" << controller.maskStack()[0].toMap().value(QStringLiteral("segmentationData")).toByteArray().size() << '\n';
    require(alpha_at(45, 60) > 240 && alpha_at(125, 60) < 10, "Fixture must contain a bounded subject");
    run(engine, QStringLiteral("root.applyEditToolActivation('brush');"));
    require(controller.selectionEditSessionActive() && controller.maskSetCount() == 1 && controller.photoLayerCount() == 0,
        "Main Brush tool must begin editing the same subject owner");
    const auto paint = [&](const QString& operation, double x) {
      controller.setActiveMaskBrushSize(0.18);
      controller.setActiveMaskBrushHardness(1.0);
      controller.setActiveMaskFeather(0.0);
      controller.beginActiveBrushSourceStroke(operation);
      controller.addBrushPoint(x, 0.5);
      controller.endActiveBrushStroke();
      settle(controller);
    };
    paint(QStringLiteral("add"), 0.78);
    require(alpha_at(125, 60) > 220 && alpha_at(45, 60) > 240,
        "Brush Add must extend the subject without replacing it");
    run(engine, QStringLiteral("root.setMaskCreationOperation('subtract'); root.applyEditToolActivation('brush');"));
    require(engine.evaluate(QStringLiteral("root.maskCreationOperation")).toString() == QStringLiteral("subtract") &&
        controller.activeMaskOperation() == QStringLiteral("subtract"), "Brush activation must preserve the selected combine mode");
    paint(QStringLiteral("subtract"), 0.28);
    require(alpha_at(45, 60) < 20 && alpha_at(125, 60) > 220, "Brush Subtract must remove subject pixels and preserve additions");
    const auto subtracted = controller.maskStack();
    controller.undo();
    require(controller.maskStack() != subtracted && alpha_at(45, 60) > 240, "Undo must restore removed subject pixels");
    controller.redo();
    require(controller.maskStack() == subtracted, "Redo must restore the exact brush subtraction");

    run(engine, QStringLiteral("root.setMaskCreationOperation('add'); root.createMaskSource('object');"));
    require(!engine.evaluate(QStringLiteral("root.objectPickCreateNew")).toBool(), "Object must continue the existing selection");
    QByteArray object_alpha(width * height, '\0');
    for (int y = 20; y < 40; ++y)
      for (int x = 105; x < 145; ++x) object_alpha[y * width + x] = char(255);
    EditorPreviewControllerTestAccess::preview(controller, object_alpha, width, height);
    require(controller.acceptSmartSelectPreview(engine.evaluate(QStringLiteral("root.objectPickMode")).toString(),
        engine.evaluate(QStringLiteral("root.objectPickCreateNew")).toBool()) == QStringLiteral("selection-ai"),
        "Object result must be accepted through the production commit path");
    require(controller.maskSetCount() == 1 && controller.photoLayerCount() == 0 &&
        alpha_at(125, 30) > 220 && alpha_at(125, 60) > 220 && alpha_at(45, 60) < 20,
        "Object Add must share the subject owner and preserve brush edits");
    const auto object_index = controller.activeMaskIndex();
    run(engine, QStringLiteral("root.createMaskSource('object'); root.setMaskCreationOperation('subtract');"));
    EditorPreviewControllerTestAccess::preview(controller, object_alpha, width, height);
    require(controller.acceptSmartSelectPreview(engine.evaluate(QStringLiteral("root.objectPickMode")).toString(),
        engine.evaluate(QStringLiteral("root.objectPickCreateNew")).toBool()) == QStringLiteral("selection-ai") &&
        alpha_at(125, 30) < 20 && alpha_at(125, 60) > 220,
        "Object Subtract must remove from the same selection without losing the brush addition");
    controller.setActiveMaskIndex(object_index);
    run(engine, QStringLiteral("root.setMaskCreationOperation('add'); root.createMaskSource('object');"));
    EditorPreviewControllerTestAccess::preview(controller, object_alpha, width, height);
    require(controller.acceptSmartSelectPreview(engine.evaluate(QStringLiteral("root.objectPickMode")).toString(), false) ==
        QStringLiteral("selection-ai") && controller.activeMaskIndex() == controller.maskCount() - 1 && alpha_at(125, 30) > 220,
        "An Object addition from an earlier source must apply after later subtractions");
    controller.setActiveMaskIndex(0);
    run(engine, QStringLiteral("root.setMaskCreationOperation('subtract'); root.startMaskBrushSource();"));
    require(controller.activeMaskIndex() == controller.maskCount() - 1,
        "A brush from an earlier source must run after all current selection components");
    paint(QStringLiteral("subtract"), 0.78);
    require(alpha_at(125, 60) < 20 && alpha_at(125, 30) > 220, "Latest subtraction must remove an earlier addition");
    for (const auto* kind : {"lasso", "polygon"}) {
      run(engine, QStringLiteral("root.handleMaskToolMenuAction('%1');").arg(QString::fromLatin1(kind)));
      require(controller.activeMaskKind() == QString::fromLatin1(kind) && controller.maskSetCount() == 1 &&
          controller.maskStack().last().toMap().value(QStringLiteral("maskSetId")) == set_id,
          "Path tools must activate inside the original selection");
    }
    run(engine, QStringLiteral("root.setMaskCreationOperation('add'); root.createMaskSource('rectangle');"));
    require(controller.maskStack().last().toMap().value(QStringLiteral("maskSetId")) == set_id &&
        controller.maskStack()[0] == original && controller.maskSetCount() == 1,
        "Other selection tools must retain the original subject and selection identity");
    require(engine.evaluate(QStringLiteral("root.maskToolKeepsActiveMask('pixel-brush') && root.maskToolKeepsActiveMask('pixel-select')")).toBool(),
        "Pixel tool activation must retain the active selection");
    // A failed generated selection must never apply its requested operation to
    // the existing active component.
    run(engine, QStringLiteral("root.setMaskCreationOperation('subtract'); root.createGeneratedSelectionSource('unsupported');"));
    require(controller.activeMaskOperation() == QStringLiteral("add"), "Failed generated masks must leave the active source unchanged");
    require(controller.finishSelectionEditSession(), "Composed selection must finish");
    const auto finished = controller.maskStack();
    const auto finished_state = recipe(controller);
    EditorPreviewController reopened;
    reopened.loadAsset(QStringLiteral("reopened"), path, {}, {}, {}, {}, serialize_edit_recipe_json(finished_state));
    settle(reopened);
    require(reopened.maskSetCount() == 1 && reopened.maskCount() == finished.size() && reopened.photoLayerCount() == 0,
        "Reopened selection must retain all components and no automatic adjustment layer");
    const auto before = evaluate_canonical_composite_mask_alpha(source, finished, 1.0, {1.0, 1.0, 1.0});
    const auto after = evaluate_canonical_composite_mask_alpha(source, reopened.maskStack(), 1.0, {1.0, 1.0, 1.0});
    require(before.valid && after.valid && before.alpha_mask == after.alpha_mask, "Reopened composed mask must have identical pixels");
    // Invert the completed group, rather than independently inverting its components.
    require(controller.invertActiveSelection(), "Invert composed selection");
    const auto inverted = evaluate_canonical_composite_mask_alpha(source, controller.maskStack(), 1.0, {1.0, 1.0, 1.0});
    require(inverted.valid, "Inverted selection must evaluate");
    for (qsizetype i = 0; i < before.alpha_mask.size(); ++i)
      require(std::abs(int(static_cast<unsigned char>(before.alpha_mask[i])) +
          int(static_cast<unsigned char>(inverted.alpha_mask[i])) - 255) <= 1,
          "Composed selection inversion must be the exact complement");
    controller.undo();
    require(controller.maskStack() == finished, "Undo inversion restores every component");
    controller.redo();
    require(controller.maskStack() != finished, "Redo restores group inversion");

    // Exercise the production tool-routing bridge with a dirty selection session.
    require(controller.addPaintLayer(), "Create the selected paint layer");
    run(engine, QStringLiteral("root.activeLayerIndex=0; root.currentPhotoLayerData=function(){return {kind:'pixel'};};"));
    QObject::connect(&controller, &EditorPreviewController::selectionEditSessionFinished,
        &engine, [&engine](bool cancelled) {
          run(engine, QStringLiteral("var pending=root.pendingSelectionAction; root.pendingSelectionAction=''; if (!%1 && pending) root.runSelectionAction(pending);")
              .arg(cancelled ? QStringLiteral("true") : QStringLiteral("false")));
        });
    require(controller.beginSelectionEditSession(), "Start another selection refinement");
    controller.setActiveMaskFeather(0.01);
    require(controller.selectionEditSessionDirty(), "Selection refinement is pending");
    run(engine, QStringLiteral("root.applyEditToolActivation('eraser');"));
    settle(controller);
    require(!controller.selectionEditSessionActive() && controller.hasActiveMask() &&
        engine.evaluate(QStringLiteral("root.editTool")).toString() == QStringLiteral("eraser") &&
        engine.evaluate(QStringLiteral("root.activeLayerIndex")).toInt() == 0,
        "Dirty selection must finish and activate Eraser on the chosen layer");
    // Switching before the asynchronous brush commit completes must resume the
    // requested pixel tool through the session-finished signal exactly once.
    require(controller.beginSelectionEditSession(), "Start asynchronous brush refinement");
    controller.beginActiveBrushSourceStroke("add");
    controller.addBrushPoint(0.7, 0.7);
    controller.endActiveBrushStroke();
    run(engine, QStringLiteral("root.applyEditToolActivation('paint');"));
    QElapsedTimer transition_timer; transition_timer.start();
    while (controller.selectionEditSessionActive() && transition_timer.elapsed() < 10000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1);
    }
    settle(controller);
    require(!controller.selectionEditSessionActive() && controller.hasActiveMask() &&
        engine.evaluate(QStringLiteral("root.editTool")).toString() == QStringLiteral("paint") &&
        engine.evaluate(QStringLiteral("root.pendingSelectionAction")).toString().isEmpty(),
        "Paint must resume after a pending brush commit without losing the selection");
    run(engine, QStringLiteral("root.applyEditToolActivation('eraser');"));
    run(engine, QStringLiteral("root.invertActiveMaskFromShortcut();"));
    require(engine.evaluate(QStringLiteral("root.editTool")).toString() == QStringLiteral("eraser"),
        "Inverting must preserve the editing tool and layer target");
    require(engine.evaluate(QStringLiteral("root.maskToolKeepsActiveMask('layers') && root.maskToolKeepsActiveMask('adjust')")).toBool(),
        "Layer and adjustment navigation must preserve the working selection");
    verify_object_intersection(source, path);
    verify_selection_tools_retarget_to_working_selection(source, path);
    verify_same_name_selection_mask_ownership(path);
    verify_layer_paint_workflow(runtime, source, path);
    std::cout << "Selection composition, group inversion, tool transitions, clipped layer painting/erasing, crop/transform, undo/redo and reopen passed\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
