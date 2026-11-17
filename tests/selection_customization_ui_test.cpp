#include <QDir>
#include <algorithm>
#include <QFile>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QJSValue>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <iostream>
#include <memory>

namespace {
void settle() { QTest::qWait(100); }
QVariant value(QVariant v) { return v.metaType() == QMetaType::fromType<QJSValue>() ? v.value<QJSValue>().toVariant() : v; }
QQuickItem* find(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name && root->isVisible()) return root;
  for (auto* child : root->childItems()) if (auto* result = find(child, name)) return result;
  return nullptr;
}
QQuickItem* find_any(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems()) if (auto* result = find_any(child, name)) return result;
  return nullptr;
}
QQuickItem* find_menu_item(QQuickItem* root, const QString& key) {
  if (!root) return nullptr;
  if (root->isVisible() && root->property("itemKey").toString() == key) return root;
  for (auto* child : root->childItems()) if (auto* result = find_menu_item(child, key)) return result;
  return nullptr;
}
void reveal(QQuickItem* item) {
  // Layouts can settle again when an active tool moves into the quick grid.
  for (int pass = 0; pass < 3; ++pass) {
    for (auto* p = item ? item->parentItem() : nullptr; p; p = p->parentItem()) {
      if (!p->property("contentY").isValid() || !p->property("contentHeight").isValid()) continue;
      const auto rect = item->mapRectToItem(p, item->boundingRect());
      const auto offset = rect.top() < 0 ? rect.top() - 8 : rect.bottom() > p->height() ? rect.bottom() - p->height() + 8 : 0;
      const auto next = std::clamp(p->property("contentY").toDouble() + offset, 0.0,
          std::max(0.0, p->property("contentHeight").toDouble() - p->height()));
      p->setProperty("contentY", next); settle();
    }
  }
}

bool click(QQuickWindow* window, const QString& name) {
  QElapsedTimer waiting; waiting.start();
  auto* target = find(window->contentItem(), name);
  while (!target && waiting.elapsed() < 2000) { settle(); target = find(window->contentItem(), name); }
  if (!target || !target->isEnabled()) { std::cerr << "Unavailable: " << name.toStdString() << '\n'; return false; }
  reveal(target);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, target->mapToScene(target->boundingRect().center()).toPoint());
  settle(); return true;
}
int fail(const char* s) { std::cerr << s << '\n'; return 1; }
void dump_refinement_state(QQuickWindow& window, QObject& fixture, const QString& output) {
  auto* refine = fixture.findChild<QObject*>(QStringLiteral("refine"));
  auto* header = find_any(window.contentItem(), QStringLiteral("refineHeader"));
  auto* original = find_any(window.contentItem(), QStringLiteral("maskInspectorShowOriginalRefinementButton"));
  auto* transform = fixture.findChild<QObject*>(QStringLiteral("selectionTransformPopup"));
  auto* menu = fixture.findChild<QObject*>(QStringLiteral("workingSelectionMenuPopup"));
  auto* focus = window.activeFocusItem();
  std::cerr << "Refinement state: expanded=" << (refine ? refine->property("expanded").toBool() : false)
            << " header=" << bool(header) << '/' << (header && header->isVisible())
            << " original=" << bool(original) << '/' << (original && original->isVisible())
            << '/' << (original && original->isEnabled())
            << " transformPopup=" << (transform ? transform->property("visible").toBool() : false)
            << " menuPopup=" << (menu ? menu->property("visible").toBool() : false)
            << " focus=" << (focus ? focus->objectName().toStdString() : "none") << '\n';
  if (!output.isEmpty()) {
    QDir().mkpath(output);
    if (!window.grabWindow().save(output + "/refinement-state-onfailure.png"))
      std::cerr << "Could not save refinement-state-onfailure.png\n";
  }
}
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("LooksyUiTests");
  QCoreApplication::setApplicationName("SelectionCustomization");
  QTemporaryDir temporary;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
  const auto output = qEnvironmentVariable("SELECTION_UI_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  QQmlEngine engine;
  QQmlComponent settingsComponent(&engine);
  settingsComponent.setData(R"qml(import QtQml
QtObject {
 property real interfaceScale: 1
 property string interfaceDensity: "balanced"
 property string systemUiFontFamily: ""
 property bool reducedMotion: true
 property bool highContrast: false
})qml", QUrl());
  std::unique_ptr<QObject> settings(settingsComponent.create());
  settings->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty("appShell", settings.get());
  QStringList warnings;
  QObject::connect(&engine, &QQmlEngine::warnings, [&](const QList<QQmlError>& errors) {
    for (const auto& error : errors) warnings.append(error.toString());
  });
  QFile inspector(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/MaskInspectorSections.qml"));
  if (!inspector.open(QIODevice::ReadOnly)) return fail("Read production refinement controls");
  const auto source = QString::fromUtf8(inspector.readAll());
  const auto section = source.indexOf("objectName: \"maskSelectionSection\"");
  const auto start = source.indexOf("        GridLayout {", section);
  const auto end = source.indexOf("        Label {\n            width: parent.width\n            text: root.selectionInverted", start);
  if (start < 0 || end < 0) return fail("Locate actual refinement layout");
  QQmlComponent fixtureComponent(&engine);
  QString fixtureText = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Window {
 id: root; visible: true; width: 340; height: 820; color: "#171717"
 property string activeTool: ""
 property string operation: "add"
 property bool selectionInverted: false
 property bool layerLocked: false
 property var editRoot: root
 property int selectedIndex: 0
 property var photoLayerRows: [
  {id:"artwork-owner", name:"Portrait", kind:"pixel", locked:layerLocked},
  {id:"adjustment-owner", name:"Color grade", kind:"adjustment", locked:false}
 ]
 property string lastAction: ""
 property bool hideMask: false
 property int targetSwitchIndex: -1
 property bool allowTargetSwitch: true
 function selectedEditTargetLayerIndex() { return selectedIndex }
 function photoLayerIndexForId(id) { return photoLayerRows.findIndex(function(layer) { return layer.id === id }) }
 function professionalLayerLocked(layer) {
  let current = layer
  for (let depth = 0; current && depth < 64; ++depth) {
   if (current.locked === true) return true
   const parentId = String(current.parentId || "")
   if (!parentId.length) return false
   current = photoLayerRows.find(function(item) { return item.id === parentId })
  }
  return true
 }
 function editPhotoLayerInspectorTarget(index) {
  targetSwitchIndex = index
  if (!allowTargetSwitch) return false
  selectedIndex = index
  editorPreview.editingLayerMask = false
  return true
 }
 function invertInspectedMaskOrSelection() { selectionInverted = !selectionInverted }
 function queueSelectionAction(action) { lastAction = action }
 function activateEditTool(tool) { lastAction = tool }
 function applySelectionToCurrentLayer(hide) { lastAction = "apply-mask"; hideMask = hide }
 function selectionCutoutDisabledReason() { return editorPreview.selectionWorkBusy ? "Busy" : "" }
 function createCutoutFromWorkingSelection(independent) { lastAction = independent ? "cutout" : "linked-cutout"; return true }
 function chooseCutoutExport() { lastAction = "export-cutout"; return true }
 function createAdjustmentFromWorkingSelection() { lastAction = "adjust-selection"; return true }
 function switchSelectionLayerTarget(key) {
  lastAction = key
  const index = photoLayerRows.findIndex(function(layer) { return "layer:" + layer.id === key })
  return index >= 0 && editPhotoLayerInspectorTarget(index)
 }
 QtObject {
  id: editorPreview; objectName: "controller"
  property bool hasWorkingSelection: true
  property bool canReselect: true
  property bool photoExportBusy: false
  property bool editingLayerMask: false
  property string maskEditLayerId: ""
  property bool selectionWorkBusy: false
  property var savedSelections: []
  property string maskRefinementPreviewMode: "refined"
  property bool maskRefinementHighQualityPreview: false
  property bool maskFullResolutionPreview: false
  property int resets: 0
  function resetActiveMaskRefinement() { resets++ }
  function activateWorkingSelection() { return true }
  function loadMaskAsSelection(id) { return true }
  function saveWorkingSelection(name) { savedSelections = savedSelections.concat([{id:"saved",name:name}]); return true }
  function transformWorkingSelection(values) { return true }
 }
 Theme { id: theme }
 ModularToolPanel {
  id: panel; objectName: "panel"; anchors.fill: parent; active: true
  title: "Selection"; targetText: "Portrait · Working selection"; contextKey: "selection"
 }
 ColumnLayout {
  parent: panel.contentItem; Layout.fillWidth: true; spacing: theme.space12
  SelectionToolbox {
   id: toolbox; objectName: "toolbox"; Layout.fillWidth: true
   activeTool: root.activeTool; objectArmed: root.activeTool === "object"
   property bool modelAvailable: false
   boxAvailable: modelAvailable
   onToolRequested: function(key) { root.activeTool = key }
   onObjectModeRequested: function(box) { objectBoxMode = box }
   onObjectCancelRequested: root.activeTool = ""
  }
  SelectionCombineBar {
   objectName: "combine"; parent: toolbox.modeContentItem; Layout.fillWidth: true
   operation: root.operation; hasSelection: editorPreview.hasWorkingSelection
   objectPicking: toolbox.objectArmed; showTips: toolbox.showTips
   onOperationRequested: function(next) { root.operation = next }
  }
  StudioCollapsibleGroup {
   id: refine; objectName: "refine"; title: "Refine edges"
   property bool open: false; expanded: open; onToggled: open = !open
   ColumnLayout { Layout.fillWidth: true
REFINEMENT
   }
  }
  SelectionContextBar { objectName: "context"; Layout.fillWidth: true; controller: editorPreview; workspace: root }
 }
})qml");
  fixtureText.replace("REFINEMENT", source.mid(start, end - start));
  fixtureComponent.setData(fixtureText.toUtf8(), QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/selection-ui-fixture.qml")));
  std::unique_ptr<QObject> fixture(fixtureComponent.create());
  if (!fixture) { std::cerr << fixtureComponent.errorString().toStdString(); return 1; }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* toolbox = fixture->findChild<QObject*>("toolbox");
  auto* controller = fixture->findChild<QObject*>("controller");
  settle();
  if (!click(window, "selectionSource_object") || fixture->property("activeTool").toString() != "object") return fail("Object tool routing");
  auto* objectTool = find(window->contentItem(), "selectionSource_object");
  if (!objectTool || !objectTool->property("checkable").toBool() ||
      !objectTool->property("checked").toBool()) return fail("Selection tools expose their selected checkable state");
  auto* box = find(window->contentItem(), "selectionObjectBoxMode");
  if (!box || box->isEnabled()) return fail("Unavailable box model must explain its disabled state");
  toolbox->setProperty("modelAvailable", true); settle();
  if (!click(window, "selectionObjectBoxMode") || !toolbox->property("objectBoxMode").toBool()) return fail("Switch object selection gesture");
  if (!output.isEmpty()) window->grabWindow().save(output + "/object-box.png");
  if (!click(window, "selectionObjectCancelButton") || !fixture->property("activeTool").toString().isEmpty()) return fail("Cancel object preview");
  if (!click(window, "selectionSource_lasso") || fixture->property("activeTool").toString() != "lasso") return fail("Switch selection tool");
  if (fixture->findChild<QObject*>(QStringLiteral("maskCombineReplaceButton")) != nullptr)
    return fail("Selection modes must retain coverage until explicit Deselect");
  for (const auto& mode : {qMakePair("maskCombineAddButton", "add"),
           qMakePair("maskCombineSubtractButton", "subtract"), qMakePair("maskCombineIntersectButton", "intersect")}) {
    if (!click(window, QString::fromLatin1(mode.first)) || fixture->property("operation").toString() != mode.second) return fail("Combine mode retains its canonical operation");
  }
  if (!click(window, "selectionCustomizeButton")) return fail("Open tool customization");
  if (!click(window, "selectionPin_polygon") || !click(window, "selectionPin_object")) return fail("Pin and unpin tools");
  if (!output.isEmpty()) window->grabWindow().save(output + "/customize.png");
  if (!click(window, "selectionTipsToggle") || toolbox->property("showTips").toBool()) return fail("Hide guidance");
  if (!click(window, "selectionCustomizeDone")) return fail("Close customization");
  const auto pins = value(toolbox->property("pinnedKeys")).toList();
  if (!pins.contains("polygon") || pins.contains("object")) return fail("Quick tools reflect customization");
  if (!click(window, "selectionSource_polygon") || fixture->property("activeTool").toString() != "polygon") return fail("Pinned tool dispatch");
  if (!click(window, "selectionAllToolsHeader")) return fail("Reveal organized full catalog");
  auto* gradient = find(window->contentItem(), "selectionSource_linear");
  if (!gradient) return fail("Grouped gradient tool remains reachable");
  reveal(gradient); gradient->forceActiveFocus(Qt::TabFocusReason);
  QTest::keyClick(window, Qt::Key_Space); settle();
  if (fixture->property("activeTool").toString() != "linear" ||
      !find(window->contentItem(), "selectionSource_linear")->hasActiveFocus() ||
      !find(window->contentItem(), "selectionSource_linear")->property("checked").toBool()) return fail("Keyboard tool selection keeps focus and checked state on the active quick tool");
  toolbox->setProperty("showTips", true);
  if (!click(window, "selectionAllToolsHeader")) return fail("Collapse the full tool catalog");
  if (!click(window, "workingSelectionMenu")) return fail("Open working selection actions");
  QElapsedTimer menuWait; menuWait.start();
  auto* transformItem = find_menu_item(window->contentItem(), QStringLiteral("transform"));
  while (!transformItem && menuWait.elapsed() < 2000) {
    settle();
    transformItem = find_menu_item(window->contentItem(), QStringLiteral("transform"));
  }
  if (!transformItem || !transformItem->isEnabled()) return fail("Transform Selection is reachable from the action menu");
  reveal(transformItem);
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    transformItem->mapToScene(transformItem->boundingRect().center()).toPoint());
  settle();
  auto* selectionMenuPopup = fixture->findChild<QObject*>("workingSelectionMenuPopup");
  if (!selectionMenuPopup || selectionMenuPopup->property("visible").toBool())
    return fail("Choosing Transform Selection dismisses the production action menu");
  for (const auto* name : {"selectionTransformMoveX", "selectionTransformMoveY",
                           "selectionTransformScale", "selectionTransformRotation"}) {
    auto* field = find(window->contentItem(), name);
    if (!field || !field->property("numericEntry").toBool() || field->width() < 60 || field->height() <= 0)
      return fail("Transform values use accessible numeric fields with responsive widths");
  }
  auto* moveXField = find(window->contentItem(), "selectionTransformMoveX");
  if (!moveXField || !moveXField->hasActiveFocus())
    return fail("Transform Selection focuses the first numeric input for keyboard editing");
  QTest::keyClick(window, Qt::Key_Escape); settle();
  auto* transformPopup = fixture->findChild<QObject*>(QStringLiteral("selectionTransformPopup"));
  QElapsedTimer popupClosing; popupClosing.start();
  while (transformPopup && transformPopup->property("visible").toBool() && popupClosing.elapsed() < 1500)
    settle();
  if (!transformPopup || transformPopup->property("visible").toBool()) {
    dump_refinement_state(*window, *fixture, output);
    return fail("Escape must dismiss Transform Selection before the inspector can be used");
  }
  if (!click(window, "refineHeader")) {
    dump_refinement_state(*window, *fixture, output);
    return fail("Open actual production refinement controls");
  }
  auto* refineGroup = fixture->findChild<QObject*>(QStringLiteral("refine"));
  if (!refineGroup || !refineGroup->property("expanded").toBool()) {
    dump_refinement_state(*window, *fixture, output);
    return fail("Clicking the refinement header must expand the real controls");
  }
  auto* beforeButton = find(window->contentItem(), "maskInspectorShowOriginalRefinementButton");
  if (!beforeButton || !beforeButton->isEnabled()) {
    dump_refinement_state(*window, *fixture, output);
    return fail("The production Before preview button must be visible and enabled after expansion");
  }
  if (!click(window, "maskInspectorShowOriginalRefinementButton") || controller->property("maskRefinementPreviewMode").toString() != "original") return fail("Before preview preserves controller routing");
  if (!click(window, "maskInspectorShowRefinedRefinementButton") || controller->property("maskRefinementPreviewMode").toString() != "refined") return fail("After preview preserves controller routing");
  if (!output.isEmpty()) window->grabWindow().save(output + "/refinement-before-quality.png");
  if (!click(window, "selectionPreviewQualityGroupHeader")) return fail("Open preview quality");
  if (!output.isEmpty()) window->grabWindow().save(output + "/quality-expanded.png");
  if (!click(window, "maskInspectorHighQualityRefinementPreviewButton") ||
      !click(window, "maskInspectorFullResolutionPreviewButton") ||
      !controller->property("maskRefinementHighQualityPreview").toBool() ||
      !controller->property("maskFullResolutionPreview").toBool()) return fail("Advanced preview quality remains reachable and edits its original controller properties");
  if (!output.isEmpty()) window->grabWindow().save(output + "/refinement.png");
  controller->setProperty("editingLayerMask", true);
  controller->setProperty("maskEditLayerId", "artwork-owner");
  settle();
  auto* targetMode = find(window->contentItem(), "selectionLayerTargetMenu");
  auto* returnToArtwork = find(window->contentItem(), "selectionEditArtworkButton");
  if (!targetMode || !targetMode->property("text").toString().contains("Portrait · Mask") ||
      !returnToArtwork || returnToArtwork->property("text").toString() != "Edit layer")
    return fail("One target control identifies the mask owner and exposes return to layer");
  if (!click(window, "selectionEditArtworkButton") || fixture->property("targetSwitchIndex").toInt() != 0 ||
      controller->property("editingLayerMask").toBool() || !controller->property("hasWorkingSelection").toBool())
    return fail("Returning from a mask targets its owner and preserves the working selection");
  controller->setProperty("editingLayerMask", true);
  controller->setProperty("maskEditLayerId", "adjustment-owner");
  settle();
  returnToArtwork = find(window->contentItem(), "selectionEditArtworkButton");
  if (!returnToArtwork || returnToArtwork->property("text").toString() != "Edit layer" ||
      !click(window, "selectionEditArtworkButton") || fixture->property("targetSwitchIndex").toInt() != 1 ||
      !controller->property("hasWorkingSelection").toBool())
    return fail("Adjustment mask return resolves the actual owner without dropping the working selection");
  controller->setProperty("editingLayerMask", true);
  controller->setProperty("maskEditLayerId", "artwork-owner");
  fixture->setProperty("allowTargetSwitch", false);
  settle();
  if (!click(window, "selectionEditArtworkButton") || !controller->property("editingLayerMask").toBool() ||
      !controller->property("hasWorkingSelection").toBool())
    return fail("A rejected artwork switch leaves the mask target and working selection intact");
  fixture->setProperty("allowTargetSwitch", true);
  if (!click(window, "selectionEditArtworkButton") || controller->property("editingLayerMask").toBool() ||
      !controller->property("hasWorkingSelection").toBool())
    return fail("A later accepted artwork switch preserves the working selection");
  fixture->setProperty("photoLayerRows", QVariant::fromValue(QVariantList{
      QVariantMap{{"id", "artwork-owner"}, {"name", "Portrait"}, {"kind", "pixel"}, {"locked", false}, {"parentId", "locked-group"}},
      QVariantMap{{"id", "locked-group"}, {"name", "Locked group"}, {"kind", "group"}, {"locked", true}}}));
  fixture->setProperty("selectedIndex", 0);
  controller->setProperty("editingLayerMask", false);
  settle();
  if (!click(window, "workingSelectionMenu")) return fail("Open selection options");
  auto* revealButton = find_menu_item(window->contentItem(), "reveal-selection");
  if (!revealButton || revealButton->isEnabled())
    return fail("Inherited group locks disable adding a selection mask");
  QTest::keyClick(window, Qt::Key_Escape); settle();
  fixture->setProperty("photoLayerRows", QVariant::fromValue(QVariantList{
      QVariantMap{{"id", "artwork-owner"}, {"name", "Portrait"}, {"kind", "pixel"}, {"locked", false}},
      QVariantMap{{"id", "adjustment-owner"}, {"name", "Color grade"}, {"kind", "adjustment"}, {"locked", false}}}));
  if (!click(window, "workingSelectionMenu")) return fail("Open mask actions");
  auto* revealSelection = find_menu_item(window->contentItem(), "reveal-selection");
  if (!revealSelection || !revealSelection->isEnabled()) return fail("Reveal selection remains reachable");
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      revealSelection->mapToScene(revealSelection->boundingRect().center()).toPoint()); settle();
  if (fixture->property("lastAction").toString() != "apply-mask" || fixture->property("hideMask").toBool()) return fail("Reveal selection routing");
  if (!click(window, "workingSelectionMenu")) return fail("Open mask options");
  auto* hideSelection = find_menu_item(window->contentItem(), QStringLiteral("hide-selection"));
  if (!hideSelection || !hideSelection->isEnabled()) return fail("Hide selection is available in mask menu");
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    hideSelection->mapToScene(hideSelection->boundingRect().center()).toPoint()); settle();
  if (!fixture->property("hideMask").toBool()) return fail("Hide selection routes to the selected layer");
  if (!click(window, "cutoutWorkingSelectionButton") || fixture->property("lastAction").toString() != "cutout")
    return fail("Cutout is a direct primary action");
  if (!click(window, "adjustWorkingSelectionButton") || fixture->property("lastAction").toString() != "adjust-selection")
    return fail("Adjust selection is an explicit primary action");
  if (!click(window, "selectionLayerTargetMenu")) return fail("Open direct layer switcher");
  auto* layerTarget = find_menu_item(window->contentItem(), QStringLiteral("layer:adjustment-owner"));
  if (!layerTarget) return fail("Direct layer switcher identifies the layer by stable ID");
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    layerTarget->mapToScene(layerTarget->boundingRect().center()).toPoint()); settle();
  if (fixture->property("selectedIndex").toInt() != 1 || !controller->property("hasWorkingSelection").toBool())
    return fail("Switch layer keeps the working selection available");
  if (!click(window, "workingSelectionDeselectButton") || fixture->property("lastAction").toString() != "deselect") return fail("Deselect routing");
  // Return to a short primary panel for the responsive matrix.
  if (!click(window, "refineHeader")) return fail("Collapse secondary controls");
  fixture->setProperty("activeTool", "rectangle"); fixture->setProperty("operation", "add");
  for (double scale : {1., 2.}) {
    settings->setProperty("interfaceScale", scale);
    for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"), QStringLiteral("comfortable")}) {
      settings->setProperty("interfaceDensity", density);
      for (int width : {260, 340}) {
        window->setWidth(qRound(width * scale)); window->setHeight(qRound(820 * scale)); settle();
        auto* scroll = fixture->findChild<QObject*>("creativeToolsScroll");
        if (scroll) if (auto* flick = qvariant_cast<QQuickItem*>(scroll->property("contentItem"))) flick->setProperty("contentY", 0);
        settle();
        auto* cutoutAction = find(window->contentItem(), "cutoutWorkingSelectionButton");
        auto* deselectAction = find(window->contentItem(), "workingSelectionDeselectButton");
        if (!cutoutAction || !deselectAction ||
            std::abs(cutoutAction->mapToScene(QPointF()).y() - deselectAction->mapToScene(QPointF()).y()) > 1)
          return fail("Primary selection actions must fit on one row at every supported sidebar width");
        auto* visualToolbox = qobject_cast<QQuickItem*>(toolbox);
        for (auto* item : visualToolbox->findChildren<QQuickItem*>()) {
          if (!item->isVisible() || !item->objectName().startsWith("selectionSource_")) continue;
          const auto rect = item->mapRectToItem(visualToolbox, item->boundingRect());
          if (rect.left() < -1 || rect.right() > visualToolbox->width() + 1) return fail("Icon tiles must stay inside the sidebar");
        }
        if (!output.isEmpty()) {
          const auto path = QStringLiteral("/selection-%1-%2-%3.png").arg(density).arg(width).arg(scale);
          if (!window->grabWindow().save(output + path)) return fail("Capture responsive selection panel");
        }
      }
    }
  }
  toolbox->setProperty("showTips", false);
  fixture.reset(); settle();
  QSettings disk; disk.sync(); QFile settingsFile(disk.fileName());
  if (!settingsFile.open(QIODevice::ReadOnly) || !settingsFile.readAll().contains("polygon")) return fail("Customization writes persistent settings");
  fixture.reset(fixtureComponent.create()); settle();
  toolbox = fixture->findChild<QObject*>("toolbox");
  if (!value(toolbox->property("pinnedKeys")).toList().contains("polygon") || toolbox->property("showTips").toBool()) return fail("Customization survives panel recreation");
  if (!warnings.isEmpty()) { std::cerr << warnings.join('\n').toStdString(); return fail("Selection UI must load without QML warnings"); }
  std::cout << "Selection tools, combine modes, customization persistence, refinement, layer actions and 12 responsive layouts passed.\n";
  return 0;
}
