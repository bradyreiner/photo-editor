#include "tablet_input_area.h"
#include <QDir>
#include <QGuiApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QJSValue>
#include <iostream>
#include <memory>

namespace {
void settle() { QTest::qWait(120); }
QVariant unpack(QVariant v) { return v.metaType() == QMetaType::fromType<QJSValue>() ? v.value<QJSValue>().toVariant() : v; }
bool click(QQuickWindow* window, QObject* object) {
  auto* item = qobject_cast<QQuickItem*>(object);
  if (!item || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
      item->mapToScene(item->boundingRect().center()).toPoint());
  settle(); return true;
}
QQuickItem* findVisual(QQuickItem* root, const QString& name) {
  if (!root) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems()) if (auto* match = findVisual(child, name)) return match;
  return nullptr;
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}
int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName("LooksyUiTests");
  QCoreApplication::setApplicationName("BrushCustomization");
  QTemporaryDir preferences;
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());
  qmlRegisterType<photo_editor::desktop::TabletInputArea>("PhotoEditor", 1, 0, "TabletInputArea");
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
    for (const auto& e : errors) warnings.append(e.toString());
  });
  QQmlComponent fixtureComponent(&engine);
  fixtureComponent.setData(R"qml(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)qml" PHOTO_EDITOR_SOURCE_DIR R"qml(/apps/desktop/qml/components"
Window {
 id: window; visible: true; width: 340; height: 820; color: "#171717"
 property bool selectionMode: false
 QtObject {
  id: mock; objectName: "mock"
  property bool editingLayerMask: false
  property bool hasWorkingSelection: false
  property bool photoPaintBusy: false
  property string photoPaintError: ""
  property var photoFragmentBrushPresetNames: []
  property int paintCalls: 0
  signal controlsChanged()
  signal previewChanged()
  function photoPaintAuthoringSize(index) { return Qt.size(2400, 1600) }
  function photoLayerSnapshot() { return [] }
  function photoFragmentCursorTipInfo(a,b) { return ({width:0,height:0}) }
  function beginPhotoPaintStroke(index, settings) { paintCalls++; return true }
  function finishPhotoPaintStroke(commit) { return true }
  function addPhotoPaintPoint(x,y,p) { paintCalls++ }
 }
 ModularToolPanel {
  id: panel; objectName: "panel"; anchors.fill: parent; active: true
  title: window.selectionMode ? "Selection brush" : "Brush"
  targetText: "Paint layer · Untitled portrait"
  contextKey: window.selectionMode ? "selection" : "paint"
 }
 Item {
  id: host; parent: panel.contentItem; Layout.fillWidth: true
  implicitHeight: childrenRect.height; visible: !window.selectionMode
 }
 PhotoPaintOverlay {
  id: paint; objectName: "paint"; x: window.width + 100; width: 100; height: 100; controlsParent: host
  active: !window.selectionMode; controller: mock
  layerData: ({kind:"pixel", id:"paint-layer", name:"Paint layer"}); layerIndex: 0
 }
 BrushToolHud {
  id: selection; objectName: "selection"; parent: panel.contentItem
  Layout.fillWidth: true; embedded: true; visible: window.selectionMode
  onFlowValueEdited: function(value) { flowValue = value }
  onHardnessValueEdited: function(value) { hardnessValue = value }
  onSizeTextAccepted: function(value) { sizePercentText = value }
 }
})qml", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/brush-ui-fixture.qml")));
  std::unique_ptr<QObject> fixture(fixtureComponent.create());
  if (!fixture) { std::cerr << fixtureComponent.errorString().toStdString(); return 1; }
  auto* window = qobject_cast<QQuickWindow*>(fixture.get());
  auto* paint = fixture->findChild<QObject*>("paint");
  auto* mock = fixture->findChild<QObject*>("mock");
  auto* picker = fixture->findChild<QObject*>("photoPaintPresets");
  if (!window || !paint || !picker) return fail("Find production brush controls");
  settle();
  const auto output = qEnvironmentVariable("BRUSH_UI_PROOF_DIR");
  if (!output.isEmpty()) QDir().mkpath(output);
  auto* preview = fixture->findChild<QQuickItem*>("photoPaintTipPreview");
  if (!preview || !click(window, fixture->findChild<QObject*>("brushPresetPickerButton"))) return fail("Open brush presets");
  auto* popup = picker->findChild<QObject*>("brushPresetPopup");
  if (!popup || !popup->property("visible").toBool()) return fail("Preset popup is visible");
  if (!output.isEmpty()) window->grabWindow().save(output + "/presets.png");
  auto* popupItem = qvariant_cast<QQuickItem*>(popup->property("contentItem"));
  if (!click(window, findVisual(popupItem, "brushBuiltinPreset1")) || paint->property("hardness").toDouble() != 0 ||
      paint->property("flow").toDouble() != 0.35) return fail("Soft preset edits actual brush settings");
  paint->setProperty("brushSize", 77);
  paint->setProperty("brushOpacity", 0.42);
  paint->setProperty("pressureFlow", true);
  paint->setProperty("textureMaterial", "graphite");
  QVariant saved;
  if (!QMetaObject::invokeMethod(picker, "savePreset", Q_RETURN_ARG(QVariant, saved), Q_ARG(QVariant, "Fine detail")) || !saved.toBool()) return fail("Save named brush");
  paint->setProperty("brushSize", 120);
  paint->setProperty("ink", QColor("#78adff"));
  paint->setProperty("erase", true);
  if (!click(window, picker->findChild<QObject*>("brushPresetPickerButton")) ||
      !click(window, findVisual(popupItem, "brushSavedPreset0"))) return fail("Choose saved preset");
  if (paint->property("brushSize").toDouble() != 77 || paint->property("brushOpacity").toDouble() != 0.42 ||
      !paint->property("pressureFlow").toBool() || paint->property("textureMaterial").toString() != "graphite") return fail("Preset restores custom stroke and material settings");
  if (!paint->property("erase").toBool() || paint->property("ink").value<QColor>() != QColor("#78adff"))
    return fail("Preset recall preserves current color and paint/erase mode");
  paint->setProperty("erase", false);
  auto* canvas = preview->findChild<QQuickItem*>("brushPracticeCanvas");
  const auto begin = canvas->mapToScene(QPointF(canvas->width()*0.2, canvas->height()*0.5)).toPoint();
  const auto end = canvas->mapToScene(QPointF(canvas->width()*0.8, canvas->height()*0.5)).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, begin);
  QTest::mouseMove(window, end, 40);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, end);
  settle();
  if (!preview->property("hasPracticeStroke").toBool() || mock->property("paintCalls").toInt() != 0) return fail("Practice stroke is interactive and leaves document untouched");
  if (!click(window, preview->findChild<QObject*>("brushPreviewReset")) || preview->property("hasPracticeStroke").toBool()) return fail("Reset practice stroke");
  if (!click(window, findVisual(window->contentItem(), "photoPaintQuickColor2")) || paint->property("ink").value<QColor>() != QColor("#ff6254")) return fail("Quick colors change actual ink");
  if (!click(window, fixture->findChild<QObject*>("photoPaintPreviewToggle")) || preview->isVisible()) return fail("Hide preview for compact controls");
  if (!output.isEmpty()) window->grabWindow().save(output + "/paint-compact-controls.png");
  click(window, fixture->findChild<QObject*>("photoPaintPreviewToggle"));
  paint->setProperty("advancedControlsExpanded", true);
  paint->setProperty("textureMaterial", "light_paint"); settle();
  if (fixture->findChild<QQuickItem*>("photoPaintTextureStrength")->isVisible() ||
      !fixture->findChild<QQuickItem*>("photoPaintLightControls")->isVisible()) return fail("Material-specific tuning stays contextual");
  paint->setProperty("textureMaterial", "graphite");
  paint->setProperty("advancedControlsExpanded", false);
  if (!click(window, picker->findChild<QObject*>("brushPresetPickerButton"))) return fail("Reopen preset popup");
  QTest::keyClick(window, Qt::Key_Escape); settle();
  if (popup->property("visible").toBool()) return fail("Escape dismisses presets");
  for (double scale : {1., 2.}) {
    settings->setProperty("interfaceScale", scale);
    for (const auto& density : {QStringLiteral("compact"), QStringLiteral("balanced"), QStringLiteral("comfortable")}) {
      settings->setProperty("interfaceDensity", density);
      for (int width : {260, 340}) {
        window->setWidth(qRound(width * scale)); window->setHeight(qRound(820 * scale));
        for (bool selectionMode : {false, true}) {
          window->setProperty("selectionMode", selectionMode); settle();
          auto* root = selectionMode ? fixture->findChild<QQuickItem*>("selection") : fixture->findChild<QQuickItem*>("photoPaintControls");
          auto* tip = fixture->findChild<QQuickItem*>(selectionMode ? "selectionBrushTipPreview" : "photoPaintTipPreview");
          if (!root || !tip || tip->width() < 120 * scale || tip->x() < -1 || tip->x()+tip->width() > root->width()+1)
            return fail("Brush practice area must fit in every layout");
          for (auto* item : root->findChildren<QQuickItem*>()) {
            if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) continue;
            if (item->objectName().startsWith("photoPaintQuickColor") || item->objectName() == "photoPaintSizeValueField") {
              const auto rect = item->mapRectToItem(root, item->boundingRect());
              if (rect.left() < -1 || rect.right() > root->width()+1) return fail("Brush controls must stay inside the panel");
            }
          }
          if (!output.isEmpty()) {
            const auto name = QStringLiteral("/%1-%2-%3-%4.png").arg(selectionMode ? "selection" : "paint").arg(density).arg(width).arg(scale);
            if (!window->grabWindow().save(output + name)) return fail("Capture native brush panel");
          }
        }
      }
    }
  }
  settings->setProperty("interfaceScale", 1.0);
  settings->setProperty("interfaceDensity", "balanced");
  window->setWidth(340); window->setHeight(820);
  window->setProperty("selectionMode", false);
  paint->setProperty("eraserTool", true); settle();
  if (fixture->findChild<QQuickItem*>("photoPaintColorButton")->isVisible() ||
      fixture->findChild<QQuickItem*>("photoPaintMaterialChooser")->isVisible()) return fail("Eraser hides paint-only options");
  if (!output.isEmpty()) window->grabWindow().save(output + "/eraser.png");
  paint->setProperty("eraserTool", false);
  mock->setProperty("editingLayerMask", true);
  paint->setProperty("advancedControlsExpanded", true); settle();
  if (fixture->findChild<QQuickItem*>("photoPaintColorButton")->isVisible() ||
      fixture->findChild<QQuickItem*>("photoPaintTextureStrength")->isVisible()) return fail("Layer mask hides color and texture options");
  if (!output.isEmpty()) window->grabWindow().save(output + "/layer-mask.png");
  mock->setProperty("editingLayerMask", false);
  paint->setProperty("advancedControlsExpanded", false);
  window->setProperty("selectionMode", true); settle();
  auto* selectionPicker = fixture->findChild<QObject*>("selectionBrushPresets");
  if (!unpack(selectionPicker->property("savedPresets")).toList().isEmpty()) return fail("Selection and paint presets remain separate");
  picker->setProperty("previewVisible", false);
  fixture.reset(); settle();
  QSettings persisted;
  persisted.sync();
  QFile savedFile(persisted.fileName());
  if (!persisted.fileName().startsWith(preferences.path()) || !savedFile.open(QIODevice::ReadOnly) ||
      !savedFile.readAll().contains("Fine detail")) return fail("Brush preset must be written to isolated persistent settings");
  fixture.reset(fixtureComponent.create()); settle();
  auto* reopenedPicker = fixture->findChild<QObject*>("photoPaintPresets");
  if (unpack(reopenedPicker->property("savedPresets")).toList().size() != 1 ||
      reopenedPicker->property("previewVisible").toBool()) return fail("Named presets and preview preference survive panel recreation");
  window = qobject_cast<QQuickWindow*>(fixture.get());
  if (!click(window, reopenedPicker->findChild<QObject*>("brushPresetPickerButton"))) return fail("Open restored presets");
  auto* reopenedPopup = reopenedPicker->findChild<QObject*>("brushPresetPopup");
  auto* reopenedContent = qvariant_cast<QQuickItem*>(reopenedPopup->property("contentItem"));
  if (!click(window, findVisual(reopenedContent, "brushSavedPreset0")) ||
      fixture->findChild<QObject*>("paint")->property("brushSize").toDouble() != 77) return fail("Recall persisted named brush");
  click(window, reopenedPicker->findChild<QObject*>("brushPresetPickerButton"));
  if (!click(window, findVisual(reopenedContent, "brushDeletePreset0")) ||
      !unpack(reopenedPicker->property("savedPresets")).toList().isEmpty()) return fail("Delete a saved brush");
  if (!warnings.isEmpty()) { std::cerr << warnings.join('\n').toStdString(); return fail("Brush UI must load without QML warnings"); }
  std::cout << "Brush preset, practice, color, compact controls and 24 responsive layouts passed.\n";
  return 0;
}
