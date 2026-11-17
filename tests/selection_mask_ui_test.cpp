#include "editor_preview_controller.h"
#include "edit_adjustment_patch.h"
#include "canonical_composite_mask_alpha.h"
#include "tablet_input_area.h"
#include <QGuiApplication>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QThread>
#include <iostream>
#include <memory>
#include <stdexcept>
using namespace photo_editor::desktop;
namespace {
QString proof_directory;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void events(int milliseconds = 30) {
  QElapsedTimer timer; timer.start();
  do { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); QThread::msleep(1); } while (timer.elapsed() < milliseconds);
}
void settle(EditorPreviewController& editor) {
  QElapsedTimer timer; timer.start();
  do { events(5); } while ((editor.processing() || editor.photoPaintBusy() || editor.selectionWorkBusy()) && timer.elapsed() < 15000);
  check(!editor.processing() && !editor.photoPaintBusy() && !editor.selectionWorkBusy(), "Editor work must settle");
  if (!editor.photoPaintError().isEmpty()) throw std::runtime_error(editor.photoPaintError().toStdString());
}
EditState state(EditorPreviewController& editor) {
  const auto result = parse_adjustment_patch_json(editor.currentAssetBoundAdjustmentPatchJson()); check(result.ok(), "Parse fixture state"); return *result.state;
}
void pointer(QQuickWindow& window, QEvent::Type type, QPointF point, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QMouseEvent event(type, point, window.mapToGlobal(point.toPoint()), type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, modifiers);
  QCoreApplication::sendEvent(&window, &event); events(12);
}
QQuickItem* item(QObject& object, const char* name) {
  auto* result = object.findChild<QQuickItem*>(QString::fromLatin1(name)); check(result, "Find production UI control"); return result;
}
void click(QQuickWindow& window, QQuickItem* target) {
  check(target->isVisible() && target->isEnabled(), "Clicked control must be visible and enabled");
  const auto point = target->mapToScene({target->width() / 2, target->height() / 2});
  pointer(window, QEvent::MouseButtonPress, point); pointer(window, QEvent::MouseButtonRelease, point);
}
void menu_click(QQuickWindow& window, QObject& root, const char* key) {
  click(window, item(root, "maskActionsPrimaryAction")); events(50);
  const auto find_row = [&](const auto& self, QQuickItem* parent) -> QQuickItem* {
    if (parent->property("itemKey").toString() == QString::fromLatin1(key) && parent->isVisible()) return parent;
    for (auto* child : parent->childItems()) if (auto* found = self(self, child)) return found;
    return nullptr;
  };
  auto* row = find_row(find_row, window.contentItem());
  if (!row) throw std::runtime_error(std::string("Find visible mask menu action: ") + key);
  if (!proof_directory.isEmpty() && QString::fromLatin1(key) == "edit-layer") {
    QDir().mkpath(proof_directory);
    QEvent leave(QEvent::Leave); QCoreApplication::sendEvent(&window, &leave); events(100);
    check(window.grabWindow().save(QDir(proof_directory).filePath("mask-menu.png")), "Capture visible mask menu");
  }
  click(window, row);
  auto* popup = root.findChild<QObject*>("maskActionsPopup");
  QElapsedTimer closing; closing.start();
  while (popup && popup->property("visible").toBool() && closing.elapsed() < 1000) events(10);
  check(popup && !popup->property("visible").toBool(), "Mask menu closes after activating its action");
}
void menu_rejects(QQuickWindow& window, QObject& root, EditorPreviewController& editor, const char* key) {
  const auto before = editor.currentAssetBoundAdjustmentPatchJson();
  const auto history = editor.editHistoryCurrentIndex();
  click(window, item(root, "maskActionsPrimaryAction")); events(50);
  const auto find_row = [&](const auto& self, QQuickItem* parent) -> QQuickItem* {
    if (parent->property("itemKey").toString() == QString::fromLatin1(key) && parent->isVisible()) return parent;
    for (auto* child : parent->childItems()) if (auto* found = self(self, child)) return found;
    return nullptr;
  };
  auto* row = find_row(find_row, window.contentItem());
  QElapsedTimer opening; opening.start();
  while (!row && opening.elapsed() < 1000) {
    events(10);
    row = find_row(find_row, window.contentItem());
  }
  if (!row || row->isEnabled()) {
    std::cerr << "disabled-menu key=" << key << " row=" << bool(row)
              << " enabled=" << (row && row->isEnabled())
              << " layers=" << state(editor).layers.size()
              << " ownerLocked=" << state(editor).layers[0].toMap().value("locked").toBool()
              << " activeMaskLocked=" << editor.activeMaskLocked() << '\n';
  }
  check(row && !row->isEnabled(), "Locked mask action must be visibly disabled");
  const auto point = row->mapToScene({row->width() / 2, row->height() / 2});
  pointer(window, QEvent::MouseButtonPress, point); pointer(window, QEvent::MouseButtonRelease, point);
  check(editor.currentAssetBoundAdjustmentPatchJson() == before && editor.editHistoryCurrentIndex() == history,
        "Clicking a disabled mask action must preserve content and history");
  auto* popup = root.findChild<QObject*>("maskActionsPopup");
  check(popup && QMetaObject::invokeMethod(popup, "close"), "Dismiss disabled-action menu");
  QElapsedTimer closing; closing.start();
  while (popup->property("visible").toBool() && closing.elapsed() < 1000) events(10);
  check(!popup->property("visible").toBool(), "Disabled-action menu finishes closing before the next gesture");
}
void drag(QQuickWindow& window, QQuickItem* source, QQuickItem* destination,
          Qt::KeyboardModifiers modifiers = Qt::NoModifier, bool coalesced = false) {
  const auto start = source->mapToScene({source->width() / 2, source->height() / 2});
  const auto end = destination->mapToScene({destination->width() * 0.75, destination->height() / 2});
  pointer(window, QEvent::MouseButtonPress, start, modifiers);
  const auto moves = coalesced ? 1 : 12;
  for (int i = 1; i <= moves; ++i) pointer(window, QEvent::MouseMove, start + (end - start) * (static_cast<double>(i) / moves), modifiers);
  pointer(window, QEvent::MouseButtonRelease, end, modifiers); events(60);
}
QVariantList masks_for_set(const QVariantList& stack, const QString& set_id) {
  QVariantList result;
  for (const auto& value : stack)
    if (value.toMap().value(QStringLiteral("maskSetId")).toString() == set_id)
      result.append(value);
  return result;
}
QByteArray coverage_for_set(const QImage& source, const QVariantList& stack, const QString& set_id) {
  const auto result = evaluate_canonical_composite_mask_alpha(
      source, masks_for_set(stack, set_id), 1.0, {1.0, 1.0, 1.0});
  check(result.valid, "Resolve canonical attached-mask coverage");
  return result.alpha_mask;
}
}
int main(int argc, char** argv) {
  QTemporaryDir runtime;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime.filePath("app").toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  if (argc > 1) proof_directory = QString::fromLocal8Bit(argv[1]);
  qmlRegisterType<TabletInputArea>("PhotoEditor", 1, 0, "TabletInputArea");
  try {
    QImage source(160, 120, QImage::Format_RGB32); source.fill(QColor("#7d8998"));
    const auto path = runtime.filePath("photo.png"); check(source.save(path), "Save UI fixture");
    EditorPreviewController editor; QQmlEngine::setObjectOwnership(&editor, QQmlEngine::CppOwnership);
    editor.loadAsset("mask-ui-proof", path, {}, {}, {}, {}, {}); settle(editor);
    editor.addMask("rectangle"); editor.setActiveMaskFeather(0); editor.setActiveMaskCenterX(0.25); editor.setActiveMaskCenterY(0.5);
    editor.setActiveMaskRadiusX(0.25); editor.setActiveMaskRadiusY(0.5);
    check(editor.photoLayerCount() == 0, "Selection alone creates no layer");
    check(editor.addPaintLayer() && editor.addAdjustmentLayer("Color"), "Create pixel and adjustment targets");
    const auto initial = state(editor);
    const auto source_id = initial.layers[0].toMap().value("id").toString(), destination_id = initial.layers[1].toMap().value("id").toString();
    check(editor.applyWorkingSelectionToLayer(0), "Attach independent mask"); settle(editor);
    const auto mask_id = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
    QFile file(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml")); check(file.open(QIODevice::ReadOnly), "Open workspace routing");
    const auto workspace = QString::fromUtf8(file.readAll());
    QString qml = QStringLiteral(R"QML(import QtQuick
import QtQuick.Controls.Basic
import "." as C
Item {
 id: root
 required property var editorPreview
 readonly property var editorPreviewController: editorPreview
 required property string sourceLayerId
 required property string destinationLayerId
 required property string sourceMaskId
 property int activeLayerIndex: 0
 property string editTool: "layers"
 readonly property bool selectionNavigationActive: editTool === "masks" || editTool === "brush"
 property var maskSessionUiBaseline: ({})
 property string editInspectorTab: "layers"
 property bool editInspectorPanelVisible: true
 property var selectedPhotoLayerIndices: [0]
 property var selectedEditTarget: ({scope:"adjustment-layer", layerIndex:0})
 property var photoLayerRows: editorPreview.photoLayers
 property var maskSetRows: editorPreview.maskSets
 property string maskCreationOperation: "add"
 property string pendingSelectionAction: ""
 property int drops: 0
 property int photoLayerSelectionAnchorIndex: 0
 property bool rejectEditTarget: false
 property bool componentWorkspaceActive: false
 property string selectedEditLayer: "adjustments"
 function selectedEditTargetLayerIndex(){return activeLayerIndex}
 function selectedEditTargetScope(){return selectedEditTarget.scope}
 function setSelectedEditTarget(scope,index,set,source){if(rejectEditTarget)return false; activeLayerIndex=index; selectedEditTarget={scope:scope,layerIndex:index,maskSetId:set.id||""}; return true}
 function photoLayerIndexForIdentity(identity){return photoLayerRows.findIndex(function(layer){return String(layer.id)===String(identity)})}
 function selectedLayerTargetScope(index){return photoLayerRows[index].kind==="adjustment"?"adjustment-layer":"photo-layer"}
 function resolveTextSession(){return true}
 function clearTransientMaskTools(){}
 function clearTransientToolStateFor(tool){}
 function normalizedActivePhotoLayerIndex(){return activeLayerIndex}
 function endMaskShapeEditTransaction(){}
 function activeMaskSetData(){return maskSetRows.find(function(set){return (set.sources||[]).some(function(s){return s.flatIndex===editorPreview.activeMaskIndex})})||({})}
 function photoLayerKind(index){return photoLayerRows[index].kind}
 function normalizedPhotoLayerSelection(){return selectedPhotoLayerIndices.slice()}
 function activateEditTool(tool){root.editTool=tool; root.editInspectorTab=tool==="adjust"?"adjust":"layers"}
 function applyEditToolActivation(tool){activateEditTool(tool)}
 function maskSetId(set){return String(set.id||"")}
 function maskSetActive(set){return (set.sources||[]).some(function(s){return s.flatIndex===editorPreview.activeMaskIndex})}
 function sourceIndex(){return photoLayerRows.findIndex(function(layer){return layer.id===sourceLayerId})}
 function destinationIndex(){return photoLayerRows.findIndex(function(layer){return layer.id===destinationLayerId})}
 function destinationMask(){return String((photoLayerRows[destinationIndex()]||{}).selectionMaskSetId||"")}
 function sourceHasMask(){return (photoLayerRows[sourceIndex()].selectionMaskSetIds||[]).indexOf(sourceMaskId)>=0}
 Connections {
  target: root.editorPreview
  function onSelectionEditSessionFinished(cancelled) {
   const action = root.pendingSelectionAction; root.pendingSelectionAction = ""
   if (!cancelled && action.length) root.runSelectionAction(action)
  }
 }
 Rectangle { anchors.fill:parent; color:"#141619" }
 C.SelectionContextBar { x:10; y:8; width:880; controller:root.editorPreview; workspace:root }
 Rectangle { x:10;y:140;width:280;height:72;color:"#24282d";border.color:"#414850"
  Text { x:48;y:42;text:"Color adjustment";color:"#f2f4f8" }
  DropArea { objectName:"sourceDrop";anchors.fill:parent; keys:["looksy-layer-mask"];onDropped:function(drop){if(root.handleLayerMaskDrop(drop.source,root.sourceLayerId)){root.drops++;drop.acceptProposedAction()}} }
  C.LayerMaskThumbnail { objectName:"sourceMaskThumbnail";x:8;y:9;width:32;height:32;maskSetId:root.sourceMaskId;sourceLayerId:root.sourceLayerId;visible:root.sourceHasMask();onActivated:root.selectLayerMaskTarget(root.sourceIndex(),maskSetId) }
  C.MenuButton { objectName:"maskActions";x:48;y:7;width:160;text:"Mask actions";primaryOpensMenu:true; menuItems:root.maskSetRowMenuItems(root.maskSetRows.find(function(set){return set.id===root.sourceMaskId})||{},root.sourceIndex(),root.sourceIndex(),0,1);onMenuItemTriggered:function(key){root.handleMaskSetRowMenuAction(key,root.maskSetRows.find(function(set){return set.id===root.sourceMaskId})||{},root.sourceIndex(),root.sourceIndex(),0)} }
 }
 Rectangle { x:10;y:232;width:280;height:72;color:"#24282d";border.color:"#414850"
  Text { x:48;y:20;text:"Pixel layer · drop mask here";color:"#f2f4f8" }
  DropArea { objectName:"destinationDrop";anchors.fill:parent;keys:["looksy-layer-mask"];onDropped:function(drop){if(root.handleLayerMaskDrop(drop.source,root.destinationLayerId)){root.drops++;drop.acceptProposedAction()}} }
  C.LayerMaskThumbnail { objectName:"destinationMaskThumbnail";x:8;y:9;width:32;height:32;maskSetId:root.destinationMask();sourceLayerId:root.destinationLayerId;visible:maskSetId.length>0;onActivated:root.selectLayerMaskTarget(root.destinationIndex(),maskSetId) }
 }
 Rectangle { x:310;y:148;width:560;height:420;color:"#667586" }
 Item { id:paintControls;x:310;y:588;width:580;height:490 }
 C.PhotoPaintOverlay {
  objectName:"maskPaintOverlay";x:300;y:138;width:580;height:440;controlsParent:paintControls
  controller:root.editorPreview;active:root.editTool==="paint"||root.editTool==="eraser";eraserTool:root.editTool==="eraser"
  layerIndex:root.activeLayerIndex;layerData:({id:root.editorPreview.maskEditLayerId,kind:"pixel",pixelWidth:160,pixelHeight:120,canvasWidth:160,canvasHeight:120,coordinateSpace:"source-area",coordinateArea:{x:0,y:0,width:1,height:1},enabled:true})
  imageX:10;imageY:10;imageWidth:560;imageHeight:420;canvasWidth:160;canvasHeight:120
  brushSize:18;hardness:1;pressureSize:false;brushOpacity:1;flow:1
 }
)QML");
    for (const auto* name : {"professionalLayerLocked", "maskSetHasLockedSources", "studioAiMaskKind", "studioMaskSupportsSelectionRefine", "selectLayerMaskTarget", "handleLayerMaskDrop", "maskSetRowMenuItems", "handleMaskSetRowMenuAction",
                           "activateBaseImageLayerTarget", "setActivePhotoLayerSelection", "setActivePhotoLayer", "selectPhotoLayerWithModifiers", "switchSelectionLayerTarget", "createAdjustmentFromWorkingSelection", "photoLayerIdentityAt", "editPhotoLayerInspectorTarget", "applySelectionToCurrentLayer", "runSelectionAction", "queueSelectionAction",
                           "photoLayerIndexForId", "normalizedPhotoLayerMaskSetValues", "photoLayerMaskSetIds",
                           "useActiveSelectionAsLayerMask", "startLayerBackgroundEraser"}) {
      const auto start = workspace.indexOf(QStringLiteral("    function %1(").arg(QString::fromLatin1(name)));
      const auto end = workspace.indexOf(QStringLiteral("\n    }"), start);
      check(start >= 0 && end > start, "Extract production UI route"); qml += workspace.mid(start, end + 6 - start);
    }
    qml += "\n}";
    QQmlEngine engine; QQmlComponent component(&engine);
    component.setData(qml.toUtf8(), QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/SelectionMaskProof.qml")));
    while (component.isLoading()) events(5);
    std::unique_ptr<QObject> object(component.createWithInitialProperties({{"editorPreview", QVariant::fromValue<QObject*>(&editor)},
        {"sourceLayerId", source_id}, {"destinationLayerId", destination_id}, {"sourceMaskId", mask_id}}));
    if (!object) std::cerr << component.errorString().toStdString(); check(bool(object), "Create real mask interaction components");
    QQuickWindow window; window.resize(900, 1080); auto* root = qobject_cast<QQuickItem*>(object.get()); root->setParentItem(window.contentItem()); root->setSize({900,1080}); window.show(); events(100);
    click(window, item(*root, "sourceMaskThumbnail"));
    check(editor.editingLayerMask() && editor.maskEditLayerId() == source_id && root->property("editTool").toString() == "paint", "Clicking a mask targets its coverage brush");
    root->setProperty("rejectEditTarget", true);
    QVariant rejected_mask_switch;
    check(QMetaObject::invokeMethod(root, "selectLayerMaskTarget",
              Q_RETURN_ARG(QVariant, rejected_mask_switch),
              Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant(mask_id))) &&
              !rejected_mask_switch.toBool() && editor.editingLayerMask() &&
              editor.maskEditLayerId() == source_id,
          "A rejected inspector transition must report failure and retain the prior mask target");
    const auto working_before_return = editor.workingSelectionSetId();
    const auto history_before_return = editor.editHistoryCurrentIndex();
    click(window, item(*root, "selectionEditArtworkButton"));
    check(editor.editingLayerMask() && root->property("editTool").toString() == "paint" &&
              editor.workingSelectionSetId() == working_before_return &&
              editor.editHistoryCurrentIndex() == history_before_return,
          "A rejected visible artwork switch retains mask, tool, working selection and history");
    QVariant invalid_switch;
    check(QMetaObject::invokeMethod(root, "setActivePhotoLayerSelection", Q_RETURN_ARG(QVariant, invalid_switch),
              Q_ARG(QVariant, QVariant(-1)), Q_ARG(QVariant, QVariant(false))) &&
              !invalid_switch.toBool() && editor.editingLayerMask(),
          "An invalid layer switch must not discard the mask target");
    root->setProperty("rejectEditTarget", false);
    click(window, item(*root, "selectionEditArtworkButton"));
    check(!editor.editingLayerMask() && root->property("editTool").toString() == "adjust" &&
              editor.workingSelectionSetId() == working_before_return &&
              editor.editHistoryCurrentIndex() == history_before_return,
          "The visible owner action returns to artwork without clearing selection or adding history");
    click(window, item(*root, "sourceMaskThumbnail"));
    const auto stale_before = editor.currentAssetBoundAdjustmentPatchJson();
    const auto stale_history = editor.editHistoryCurrentIndex();
    QVariantMap stale_set;
    for (const auto& value : editor.maskSets())
      if (value.toMap().value("id").toString() == mask_id) stale_set = value.toMap();
    check(!stale_set.isEmpty(), "Resolve the stale menu's original mask identity");
    for (const auto* action : {"invert-mask", "remove-mask", "apply-selection"}) {
      QVariant rejected;
      check(QMetaObject::invokeMethod(root, "handleMaskSetRowMenuAction", Q_RETURN_ARG(QVariant, rejected),
                Q_ARG(QVariant, QVariant(QString::fromLatin1(action))), Q_ARG(QVariant, QVariant(stale_set)),
                Q_ARG(QVariant, QVariant(1)), Q_ARG(QVariant, QVariant(1)), Q_ARG(QVariant, QVariant(0))) &&
                !rejected.toBool(), "A menu callback with a stale owner must be rejected");
    }
    check(editor.currentAssetBoundAdjustmentPatchJson() == stale_before && editor.editHistoryCurrentIndex() == stale_history,
          "Stale menu callbacks preserve authored state and history");
    menu_click(window, *root, "edit-layer");
    check(!editor.editingLayerMask() && root->property("editTool").toString() == "adjust", "Mask dropdown Edit Layer selects color controls without changing mask");
    check(editor.setPhotoLayerAdjustmentValue(0, "exposure", 0.6), "Color edit applies to adjustment target");
    drag(window, item(*root, "sourceMaskThumbnail"), item(*root, "destinationDrop")); settle(editor);
    check(root->property("drops").toInt() == 1 && state(editor).layers[1].toMap().value("selectionMaskSetId").toString() == mask_id,
        "Actual pointer drag must move mask from adjustment row to pixel row");
    drag(window, item(*root, "destinationMaskThumbnail"), item(*root, "sourceDrop"), Qt::AltModifier); settle(editor);
    check(root->property("drops").toInt() == 2 && state(editor).layers[0].toMap().value("selectionMaskSetId").toString() != mask_id &&
        state(editor).layers[1].toMap().value("selectionMaskSetId").toString() == mask_id, "Alt-drag copies independently and retains source");
    const auto fast_copy_id = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
    root->setProperty("sourceMaskId", fast_copy_id);
    drag(window, item(*root, "sourceMaskThumbnail"), item(*root, "destinationDrop"), Qt::NoModifier, true);
    settle(editor);
    check(root->property("drops").toInt() == 3 &&
              state(editor).layers[1].toMap().value("selectionMaskSetIds").toList().contains(fast_copy_id),
          "A coalesced single-move pointer drag must transfer the captured mask");
    check(editor.transferLayerMask(destination_id, fast_copy_id, source_id, false), "Restore fixture ownership after fast drag");
    settle(editor);
    auto* identity_source = item(*root, "sourceMaskThumbnail");
    auto* identity_target = item(*root, "destinationDrop");
    const auto identity_start = identity_source->mapToScene({identity_source->width()/2, identity_source->height()/2});
    const auto identity_end = identity_target->mapToScene({identity_target->width()*0.75, identity_target->height()/2});
    pointer(window, QEvent::MouseButtonPress, identity_start);
    pointer(window, QEvent::MouseMove, identity_end);
    // Keep the delegate exposed while its model data changes. Hiding it would
    // cancel the pointer grab rather than exercise the captured drag payload.
    identity_source->setProperty("maskSetId", QStringLiteral("delegate-rebound-after-press"));
    identity_source->setProperty("vectorMask", true); events(20);
    pointer(window, QEvent::MouseButtonRelease, identity_end); settle(editor);
    check(root->property("drops").toInt() == 4 &&
              state(editor).layers[1].toMap().value("selectionMaskSetIds").toList().contains(fast_copy_id),
          "Changing a thumbnail binding during drag must not retarget the captured source identity");
    check(editor.transferLayerMask(destination_id, fast_copy_id, source_id, false), "Restore fixture after captured-identity drag");
    identity_source->setProperty("maskSetId", fast_copy_id);
    identity_source->setProperty("vectorMask", false);
    root->setProperty("sourceMaskId", fast_copy_id); settle(editor);
    click(window, item(*root, "destinationMaskThumbnail"));
    const auto before = state(editor);
    click(window, item(*root, "photoPaintEraseMode"));
    auto* overlay = item(*root, "maskPaintOverlay");
    const auto position = overlay->mapToScene({10 + 40.5 / 160 * 560, 10 + 60.5 / 120 * 420});
    pointer(window, QEvent::MouseButtonPress, position); pointer(window, QEvent::MouseButtonRelease, position); settle(editor);
    QVariantList edited_mask;
    for (const auto& value : editor.maskStack()) if (value.toMap().value("maskSetId").toString() == mask_id) edited_mask.append(value);
    const auto painted = evaluate_canonical_composite_mask_alpha(source, edited_mask, 1.0, {1,1,1});
    check(painted.valid && painted.alpha_mask[60*160+40] == '\0' && state(editor).layers == before.layers,
        "Actual Hide pointer gesture edits only the selected mask and preserves both layers");
    click(window, item(*root, "workingSelectionDeselectButton"));
    check(!editor.hasWorkingSelection() && editor.editingLayerMask(), "Visible Deselect keeps layer-mask target active");
    QVariant can_refine_attached_mask;
    check(QMetaObject::invokeMethod(root, "studioMaskSupportsSelectionRefine", Q_RETURN_ARG(QVariant, can_refine_attached_mask)) &&
              can_refine_attached_mask.toBool(),
          "An attached mask remains refinable after its independent working selection is deselected");
    QEvent leave(QEvent::Leave);
    QCoreApplication::sendEvent(&window, &leave); events(200);
    if (argc > 1) { QDir().mkpath(QString::fromLocal8Bit(argv[1])); events(3200); check(window.grabWindow().save(QDir(QString::fromLocal8Bit(argv[1])).filePath("mask-interactions.png")), "Capture presented component proof"); }
    const auto copied_id = state(editor).layers[0].toMap().value("selectionMaskSetId").toString();
    root->setProperty("sourceMaskId", copied_id);
    menu_click(window, *root, "edit-mask");
    check(editor.editingLayerMask() && editor.maskEditLayerId() == source_id, "Edit Mask menu selects its exact owner");
    menu_click(window, *root, "invert-mask");
    check(state(editor).mask_stack.last().toMap().value("maskSetSelectionInvert").toBool(), "Invert Mask menu changes mask orientation");
    menu_click(window, *root, "invert-mask");
    menu_click(window, *root, "toggle-mask");
    check(!state(editor).layers[0].toMap().value("maskEnabled").toBool(), "Disable Mask menu bypasses layer mask");
    menu_click(window, *root, "toggle-mask");
    check(state(editor).layers[0].toMap().value("maskEnabled").toBool(), "Enable Mask menu restores layer mask");
    menu_click(window, *root, "load-selection"); settle(editor);
    check(editor.hasWorkingSelection() && editor.workingSelectionSetId() != copied_id, "Load as Selection creates independent working coverage");
    const auto count_before_attach = state(editor).layers[0].toMap().value("selectionMaskSetIds").toList().size();
    menu_click(window, *root, "apply-selection"); settle(editor);
    check(state(editor).layers[0].toMap().value("selectionMaskSetIds").toList().size() == count_before_attach + 1,
        "Add to Selected Layer menu attaches an independent mask");
    check(editor.setPhotoLayerLocked(0, true), "Lock owner for mask menu rejection");
    for (const auto* key : {"invert-mask", "remove-mask", "apply-selection"}) menu_rejects(window, *root, editor, key);
    menu_click(window, *root, "edit-mask");
    check(editor.editingLayerMask(), "A locked mask owner still permits inspection");
    check(editor.setPhotoLayerLocked(0, false), "Unlock owner after mask menu rejection");
    editor.setActiveMaskLocked(true); settle(editor);
    for (const auto* key : {"invert-mask", "remove-mask", "apply-selection"}) menu_rejects(window, *root, editor, key);
    editor.setActiveMaskLocked(false); settle(editor);
    menu_click(window, *root, "remove-mask"); settle(editor);
    check(!state(editor).layers[0].toMap().value("selectionMaskSetIds").toList().contains(copied_id) && editor.hasWorkingSelection(),
        "Remove from Layer detaches only that mask and preserves the working selection");

    const auto working_id = editor.workingSelectionSetId();
    check(!working_id.isEmpty(), "Background eraser routing needs a retained working selection");
    const auto check_background_eraser_target = [&](const QString& kind) {
      editor.finishLayerMaskEditing();
      bool created = false;
      if (kind == QStringLiteral("pixel")) created = editor.addPaintLayer();
      else if (kind == QStringLiteral("fill")) created = editor.addFillLayer(QStringLiteral("#d48a44"), QStringLiteral("Maskable fill"));
      else if (kind == QStringLiteral("text")) created = editor.addTextLayer(QStringLiteral("Mask target"), QStringLiteral("Maskable text"));
      check(created, "Create a target kind for the background eraser route");
      settle(editor); events(50);
      QString target_id;
      for (const auto& value : state(editor).layers) {
        const auto layer = value.toMap();
        if (layer.value(QStringLiteral("kind")).toString() == kind &&
            (kind == QStringLiteral("pixel") || layer.value(QStringLiteral("name")).toString() == QStringLiteral("Maskable %1").arg(kind))) {
          target_id = layer.value(QStringLiteral("id")).toString();
          break;
        }
      }
      check(!target_id.isEmpty(), "Resolve the newly created background eraser owner");
      const auto target_index = [&] {
        const auto layers = state(editor).layers;
        for (int index = 0; index < layers.size(); ++index)
          if (layers[index].toMap().value(QStringLiteral("id")).toString() == target_id) return index;
        return -1;
      }();
      check(target_index >= 0 && editor.hasWorkingSelection() && editor.workingSelectionSetId() == working_id,
          "The separate working selection must remain active before attachment");
      if (kind == QStringLiteral("pixel")) {
        check(editor.applyWorkingSelectionToLayer(target_index),
            "Seed a pre-existing intersection before using Background Eraser");
        settle(editor);
        auto seeded = state(editor);
        auto seeded_layer = seeded.layers[target_index].toMap();
        seeded_layer.insert(QStringLiteral("selectionIntersectionMaskSetIds"),
            seeded_layer.value(QStringLiteral("selectionMaskSetIds")));
        seeded_layer.insert(QStringLiteral("selectionIntersectionMaskSetNames"),
            seeded_layer.value(QStringLiteral("selectionMaskSetNames")));
        for (const auto* key : {"selectionMaskSetId", "selectionMaskSetIds", "selectionMaskSetName", "selectionMaskSetNames"})
          seeded_layer.remove(QLatin1String(key));
        seeded.layers[target_index] = seeded_layer;
        editor.loadAsset(QStringLiteral("background-eraser-existing-intersection"), path,
            {}, {}, {}, {}, serialize_edit_recipe_json(seeded));
        settle(editor); events(50);
      }
      editor.setActiveMaskIndex(-1);
      check(editor.hasWorkingSelection() && !editor.hasActiveMask(),
          "A retained working selection must be usable while a layer is inspected without an active mask");
      const auto before_attach = state(editor);
      const auto working_sources = masks_for_set(before_attach.mask_stack, working_id);
      check(!working_sources.isEmpty(), "Retained working selection must have authored coverage");
      root->setProperty("activeLayerIndex", target_index);
      root->setProperty("selectedEditTarget", QVariantMap{{QStringLiteral("scope"), QStringLiteral("photo-layer")},
          {QStringLiteral("layerIndex"), target_index}});
      QVariant started;
      check(QMetaObject::invokeMethod(root, "startLayerBackgroundEraser", Q_RETURN_ARG(QVariant, started),
          Q_ARG(QVariant, QVariant(target_index))) && started.toBool(),
          "Production Erase Background route must start for pixel, fill and text layers");
      settle(editor); events(80);
      const auto attached = state(editor);
      const auto attached_layer = attached.layers[target_index].toMap();
      const auto attached_ids = attached_layer.value(QStringLiteral("selectionMaskSetIds")).toList();
      check(attached_layer.value(QStringLiteral("selectionIntersectionMaskSetIds")) ==
                before_attach.layers[target_index].toMap().value(QStringLiteral("selectionIntersectionMaskSetIds")),
          "Background Eraser must preserve pre-existing intersection attachments");
      check(editor.editingLayerMask() && editor.maskEditLayerId() == target_id &&
          root->property("editTool").toString() == QStringLiteral("eraser") &&
          !attached_ids.isEmpty() && attached_ids.last().toString() != working_id,
          "Background eraser must target the newly attached mask while keeping the eraser tool active");
      const auto attached_mask_id = attached_ids.last().toString();
      const auto before_mask_sources = masks_for_set(attached.mask_stack, attached_mask_id);
      const auto before_mask_alpha = coverage_for_set(source, attached.mask_stack, attached_mask_id);
      const auto attached_recipe = editor.currentAssetBoundAdjustmentPatchJson();
      check(before_mask_alpha.size() == source.width() * source.height() &&
          static_cast<unsigned char>(before_mask_alpha[60 * source.width() + 40]) > 240,
          "New attached mask must initially retain visible coverage inside the selection");
      for (const auto& value : before_attach.mask_stack) {
        const auto other_id = value.toMap().value(QStringLiteral("maskSetId")).toString();
        if (!other_id.isEmpty() && other_id != attached_mask_id)
          check(masks_for_set(attached.mask_stack, other_id) == masks_for_set(before_attach.mask_stack, other_id),
              "Attaching a mask must preserve every existing mask source");
      }
      check(editor.workingSelectionSetId() == working_id && masks_for_set(attached.mask_stack, working_id) == working_sources,
          "Attaching the working selection must preserve its independent identity and sources");

      auto* paint_overlay = item(*root, "maskPaintOverlay");
      const auto erase_point = paint_overlay->mapToScene({10 + 40.5 / 160.0 * 560.0,
                                                          10 + 60.5 / 120.0 * 420.0});
      pointer(window, QEvent::MouseButtonPress, erase_point);
      pointer(window, QEvent::MouseButtonRelease, erase_point);
      settle(editor);
      const auto after_stroke = state(editor);
      const auto after_mask_sources = masks_for_set(after_stroke.mask_stack, attached_mask_id);
      const auto after_mask_alpha = coverage_for_set(source, after_stroke.mask_stack, attached_mask_id);
      check(!after_mask_sources.isEmpty() && before_mask_sources != after_mask_sources &&
          after_mask_alpha[60 * source.width() + 40] == '\0',
          "Actual pointer erasing must change only coverage on the selected attached mask");
      check(after_stroke.layers == attached.layers &&
          editor.workingSelectionSetId() == working_id &&
          masks_for_set(after_stroke.mask_stack, working_id) == working_sources,
          "Mask erasing must preserve owner content and the separate working selection");
      for (const auto& value : before_attach.mask_stack) {
        const auto other_id = value.toMap().value(QStringLiteral("maskSetId")).toString();
        if (!other_id.isEmpty() && other_id != attached_mask_id)
          check(masks_for_set(after_stroke.mask_stack, other_id) == masks_for_set(before_attach.mask_stack, other_id),
              "Mask erasing must not change unrelated mask sets");
      }
      const auto painted_recipe = editor.currentAssetBoundAdjustmentPatchJson();
      editor.undo(); settle(editor);
      check(editor.currentAssetBoundAdjustmentPatchJson() == attached_recipe &&
          editor.editingLayerMask() && editor.maskEditLayerId() == target_id,
          "Undo must restore the new attached mask exactly and keep its target selected");
      editor.redo(); settle(editor);
      check(editor.currentAssetBoundAdjustmentPatchJson() == painted_recipe &&
          coverage_for_set(source, state(editor).mask_stack, attached_mask_id) == after_mask_alpha,
          "Redo must restore the exact eraser coverage on the same attached mask");
      editor.finishLayerMaskEditing();
      root->setProperty("editTool", QStringLiteral("layers"));
    };
    for (const auto* kind : {"pixel", "fill", "text"})
      check_background_eraser_target(QString::fromLatin1(kind));
    check(editor.setBaseImageLocked(false), "Unlock the base image to create its editable owner");
    settle(editor); events(50);
    int base_index = -1;
    QString base_mask_id;
    for (int i = 0; i < editor.photoLayers().size(); ++i)
      if (editor.photoLayers()[i].toMap().value("baseImageEdit").toBool()) base_index = i;
    check(base_index >= 0 && editor.applyWorkingSelectionToLayer(base_index), "Attach working coverage to the editable base image");
    settle(editor); events(50);
    base_mask_id = state(editor).layers[base_index].toMap().value("selectionMaskSetId").toString();
    QVariant base_selected;
    check(QMetaObject::invokeMethod(root, "selectLayerMaskTarget", Q_RETURN_ARG(QVariant, base_selected),
              Q_ARG(QVariant, QVariant(base_index)), Q_ARG(QVariant, QVariant(base_mask_id))) && base_selected.toBool(),
          "Select the base image's actual layer mask");
    const auto base_working = editor.workingSelectionSetId();
    const auto base_history = editor.editHistoryCurrentIndex();
    root->setProperty("rejectEditTarget", true);
    click(window, item(*root, "selectionEditArtworkButton"));
    check(editor.editingLayerMask() && editor.workingSelectionSetId() == base_working &&
              root->property("editTool").toString() == "paint" && editor.editHistoryCurrentIndex() == base_history,
          "Rejected base-image navigation preserves mask, tool, working selection and history");
    root->setProperty("rejectEditTarget", false);
    click(window, item(*root, "selectionEditArtworkButton"));
    check(!editor.editingLayerMask() && root->property("selectedEditTarget").toMap().value("scope").toString() == "base-image" &&
              editor.workingSelectionSetId() == base_working && editor.editHistoryCurrentIndex() == base_history,
          "Base-image navigation exits mask editing and retains the working selection without a history edit");
    check(editor.reselectWorkingSelection() || editor.hasWorkingSelection(), "Restore selection for layer switching");
    const auto retained_selection = editor.workingSelectionSetId();
    const auto switch_layers_before = state(editor).layers;
    for (const auto* route : {"switchSelectionLayerTarget", "selectPhotoLayerWithModifiers", "editPhotoLayerInspectorTarget"}) {
      root->setProperty("editTool", "brush");
      check(editor.beginSelectionEditSession(), "Begin real selection session before switching layers");
      QVariant switched;
      const auto target_index = root->property("destinationLayerId").toString().isEmpty() ? -1 :
          [&] { const auto rows = editor.photoLayers(); for (int i = 0; i < rows.size(); ++i)
            if (rows[i].toMap().value("id").toString() == destination_id) return i; return -1; }();
      check(target_index >= 0, "Find stable destination identity after mask workflow");
      if (QString::fromLatin1(route) == "switchSelectionLayerTarget") {
        check(QMetaObject::invokeMethod(root, route, Q_RETURN_ARG(QVariant, switched),
                  Q_ARG(QVariant, QVariant(QStringLiteral("layer:") + destination_id))), "Invoke direct layer switch");
      } else if (QString::fromLatin1(route) == "selectPhotoLayerWithModifiers") {
        check(QMetaObject::invokeMethod(root, route, Q_RETURN_ARG(QVariant, switched),
                  Q_ARG(QVariant, QVariant(target_index)), Q_ARG(QVariant, QVariant(0))), "Invoke layer-row switch");
      } else {
        check(QMetaObject::invokeMethod(root, route, Q_RETURN_ARG(QVariant, switched),
                  Q_ARG(QVariant, QVariant(target_index))), "Invoke layer inspector switch");
      }
      settle(editor); events(50);
      const bool retained_session = QString::fromLatin1(route) != "editPhotoLayerInspectorTarget";
      check(switched.toBool() && editor.selectionEditSessionActive() == retained_session && !editor.editingLayerMask() &&
                editor.workingSelectionSetId() == retained_selection && state(editor).layers == switch_layers_before &&
                root->property("activeLayerIndex").toInt() == target_index,
          "Target navigation preserves refinement; explicit inspector editing commits it without changing layers");
      if (retained_session) {
        check(root->property("editTool").toString() == "brush", "Target navigation keeps the brush tool");
        check(editor.finishSelectionEditSession(), "Finish retained session after target navigation check");
        settle(editor);
      }
    }

    std::cout << "Every visible mask menu action, layer/color/base target transitions, stale-owner guards, drag move, Alt-drag copy, mask Hide pointer input and separate Deselect passed\n";
    root->setParentItem(nullptr); window.close(); return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
