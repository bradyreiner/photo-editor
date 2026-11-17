#include <QDir>
#include <QByteArray>
#include <QFile>
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <utility>

namespace {

void settle() { QTest::qWait(40); }

bool capture_window(QQuickWindow* window, const QString& path) {
  if (window == nullptr) return false;
  QTest::mouseMove(window, QPoint(1, 1));
  settle();
  if (QGuiApplication::platformName() == QStringLiteral("cocoa") && !window->isExposed())
    return false;
  return window->grabWindow().save(path);
}

QQuickItem* find_visual(QQuickItem* root, const QString& name) {
  if (root == nullptr) return nullptr;
  if (root->objectName() == name) return root;
  for (auto* child : root->childItems()) {
    if (auto* match = find_visual(child, name)) return match;
  }
  return nullptr;
}

bool click(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible() || !item->isEnabled()) return false;
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    item->mapToScene(item->boundingRect().center()).toPoint());
  settle();
  return true;
}

bool descendants_fit(QQuickItem* item_parent, QQuickItem* hud, QString* failure) {
  if (item_parent == nullptr || hud == nullptr) return false;
  for (auto* item : item_parent->childItems()) {
    if (!item->isVisible() || item->width() <= 0 || item->height() <= 0) continue;
    const auto rect = item->mapRectToItem(hud, item->boundingRect());
    if (rect.left() < -1 || rect.top() < -1 || rect.right() > hud->width() + 1 ||
        rect.bottom() > hud->height() + 1) {
      if (failure != nullptr) {
        *failure = QStringLiteral("%1 bounds %2,%3 %4x%5 exceed HUD %6x%7")
                       .arg(item->objectName())
                       .arg(rect.x())
                       .arg(rect.y())
                       .arg(rect.width())
                       .arg(rect.height())
                       .arg(hud->width())
                       .arg(hud->height());
      }
      return false;
    }
    if (item->objectName() == QStringLiteral("retouchHudScroll")) continue;
    if (!descendants_fit(item, hud, failure)) return false;
  }
  return true;
}

bool item_inside(QQuickItem* item, QQuickItem* hud) {
  if (item == nullptr || hud == nullptr) return false;
  const auto rect = item->mapRectToItem(hud, item->boundingRect());
  return rect.left() >= -1 && rect.top() >= -1 &&
         rect.right() <= hud->width() + 1 && rect.bottom() <= hud->height() + 1;
}

bool readable_button(QQuickItem* button) {
  return button != nullptr && button->isVisible() &&
         button->property("contentFitsControl").toBool() &&
         !button->property("labelOverflowing").toBool();
}

QString qml_function(const QString& source, const QString& name) {
  const auto start = source.indexOf(QStringLiteral("    function %1(").arg(name));
  if (start < 0) return {};
  auto end = source.indexOf(QStringLiteral("\n    function "), start + 1);
  if (end < 0) end = source.indexOf(QStringLiteral("\n    property "), start + 1);
  if (end < 0) return {};
  return source.mid(start, end - start);
}

int fail(const QString& message) {
  std::cerr << message.toStdString() << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("RetouchToolHud"));

  QTemporaryDir preferences;
  if (!preferences.isValid()) return fail(QStringLiteral("Create isolated retouch HUD preferences"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, preferences.path());

  QQmlEngine engine;
  QQmlComponent settings_component(&engine);
  settings_component.setData(R"qml(import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
})qml", QUrl());
  std::unique_ptr<QObject> app_shell(settings_component.create());
  if (!app_shell) return fail(settings_component.errorString());
  app_shell->setProperty("systemUiFontFamily", app.font().family());
  engine.rootContext()->setContextProperty(QStringLiteral("appShell"), app_shell.get());

  const QByteArray qml = QByteArrayLiteral(R"qml(
import QtQuick
import QtQuick.Window
import "file:)qml") + QByteArray(PHOTO_EDITOR_SOURCE_DIR) + QByteArrayLiteral(R"qml(/apps/desktop/qml/components" as Components

Window {
    id: fixture
    objectName: "retouchHudFixture"
    visible: true
    width: 900
    height: 1000
    color: "#161b20"
    property string selectedMode: ""
    property string selectedInlineAction: ""
    property int cloneSourceRequests: 0

    QtObject {
        id: controller
        objectName: "retouchHudMockController"
        property string activeMaskRetouchMode: "remove"
        property bool activeMaskCommitted: false
        property bool inpaintProgressActive: false
        property string inpaintProgressLabel: ""
        property bool activeMaskCloneSourceSet: false
        property bool activeMaskLocked: false
        property real activeMaskBrushSize: 0.08
        property real activeMaskBrushHardness: 0.25
        property real activeMaskBrushFlow: 1.0
        property int retouchCount: 1
        property bool retouchBeforeAfterEnabled: true
    }

    Components.RetouchToolHud {
        id: hud
        objectName: "retouchToolHud"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        width: Math.min(parent.width - 24, implicitWidth)
        previewController: controller
        toolLabel: "Retouch"
        statusText: "Remove / Paint area / After"
        primaryText: "Apply"
        primaryTooltip: "Apply this repair to the edit recipe."
        primaryEnabled: true
        modeMenuItems: [{ key: "retouch-dust", label: "Dust Cleanup", icon: "scope" },
                        { key: "retouch-red-eye", label: "Red Eye", icon: "adjust" }]
        inlineActions: [{ key: "remove-add", objectName: "retouchHudRemoveAddButton",
                          label: "Add", icon: "brush-tool", role: "segmented", checked: true }]
        moreMenuItems: [{ key: "texture-auto", label: "Fill texture: automatic" }]
        onModeSelected: function(mode) {
            fixture.selectedMode = mode
            controller.activeMaskRetouchMode = mode
        }
        onSecondaryModeItemTriggered: function(key) {
            if (key === "retouch-dust") controller.activeMaskRetouchMode = "dust"
            if (key === "retouch-red-eye") controller.activeMaskRetouchMode = "red-eye"
        }
        onInlineActionTriggered: function(key) { fixture.selectedInlineAction = key }
        onCloneSourceRequested: fixture.cloneSourceRequests += 1
    }
}
)qml");

  QQmlComponent component(&engine);
  component.setData(qml, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/retouch_tool_hud_ui_test.cpp")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) return fail(component.errorString());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  auto* hud = find_visual(window != nullptr ? window->contentItem() : nullptr, QStringLiteral("retouchToolHud"));
  if (window == nullptr || hud == nullptr) return fail(QStringLiteral("Create the Retouch HUD with its mocked controller"));
  window->requestActivate();
  settle();

  const auto hud_contract = hud->property("hudContract").toString();
  const auto workflow_contract = hud->property("retouchHudContract").toString();
  if (!hud_contract.contains(QStringLiteral("specialized-tool-hud:v4|tool:retouch|component:RetouchToolHud")) ||
      !workflow_contract.contains(QStringLiteral("modes:remove-heal-patch-clone")) ||
      !workflow_contract.contains(QStringLiteral("quick-brush:size-presets"))) {
    return fail(QStringLiteral("Expose the grouped Retouch HUD contract"));
  }

  auto* fixture = object.get();
  auto* controller = fixture->findChild<QObject*>(QStringLiteral("retouchHudMockController"));
  auto* remove = find_visual(hud, QStringLiteral("retouchHudRemoveButton"));
  auto* heal = find_visual(hud, QStringLiteral("retouchHudHealButton"));
  auto* patch = find_visual(hud, QStringLiteral("retouchHudPatchButton"));
  auto* clone = find_visual(hud, QStringLiteral("retouchHudCloneButton"));
  auto* source = find_visual(hud, QStringLiteral("retouchHudClonePickSourceButton"));
  auto* active_mode_label = find_visual(hud, QStringLiteral("retouchHudActiveModeLabel"));
  auto* before = find_visual(hud, QStringLiteral("retouchHudReviewBeforeButton"));
  auto* after = find_visual(hud, QStringLiteral("retouchHudReviewAfterButton"));
  auto* size = find_visual(hud, QStringLiteral("retouchHudBrushSizeSlider"));
  auto* add = find_visual(hud, QStringLiteral("retouchHudRemoveAddButton"));
  auto* preset_button = find_visual(hud, QStringLiteral("brushPresetPickerButton"));
  auto* secondary_mode_menu = find_visual(hud, QStringLiteral("retouchHudModeMenuButton"));
  if (controller == nullptr || remove == nullptr || heal == nullptr || patch == nullptr || clone == nullptr ||
      source == nullptr || active_mode_label == nullptr || before == nullptr || after == nullptr ||
      size == nullptr || add == nullptr || preset_button == nullptr || secondary_mode_menu == nullptr) {
    return fail(QStringLiteral("Expose the four modes, source picker, brush, review, and refine controls"));
  }

  for (auto* item : {remove, heal, patch, clone}) {
    if (!item->isVisible() || item->property("showLeadingIcon").toBool() != true) {
      return fail(QStringLiteral("Every primary retouch method needs a visible label and icon"));
    }
  }
  if (!click(window, heal) || fixture->property("selectedMode").toString() != QStringLiteral("heal")) {
    std::cerr << "Heal click state: visible=" << heal->isVisible()
              << " enabled=" << heal->isEnabled()
              << " rect=" << heal->x() << ',' << heal->y() << ' '
              << heal->width() << 'x' << heal->height()
              << " scene=" << heal->mapToScene(heal->boundingRect().center()).x() << ','
              << heal->mapToScene(heal->boundingRect().center()).y()
              << " window=" << window->width() << 'x' << window->height()
              << " selected=" << fixture->property("selectedMode").toString().toStdString() << '\n';
    return fail(QStringLiteral("Heal tile starts the existing Heal mode callback"));
  }
  if (!click(window, patch) || fixture->property("selectedMode").toString() != QStringLiteral("patch")) {
    return fail(QStringLiteral("Patch tile starts the existing Patch mode callback"));
  }
  if (!click(window, clone) || fixture->property("selectedMode").toString() != QStringLiteral("clone")) {
    return fail(QStringLiteral("Clone tile starts the existing Clone mode callback"));
  }
  controller->setProperty("activeMaskRetouchMode", QStringLiteral("clone"));
  settle();
  if (!source->isVisible() || !click(window, source) || fixture->property("cloneSourceRequests").toInt() != 1) {
    return fail(QStringLiteral("Clone mode exposes and triggers the existing source picker"));
  }
  if (!click(window, after) || controller->property("retouchBeforeAfterEnabled").toBool()) {
    return fail(QStringLiteral("After review control switches to the existing after view"));
  }
  if (!click(window, before) || !controller->property("retouchBeforeAfterEnabled").toBool()) {
    return fail(QStringLiteral("Before review control switches to the existing before view"));
  }
  controller->setProperty("activeMaskRetouchMode", QStringLiteral("remove"));
  settle();
  if (!click(window, add) || fixture->property("selectedInlineAction").toString() != QStringLiteral("remove-add")) {
    return fail(QStringLiteral("Remove Add refine control keeps its existing action callback"));
  }
  controller->setProperty("activeMaskBrushSize", 0.16);
  settle();
  if (qAbs(size->property("value").toDouble() - 0.16) > 0.005) {
    return fail(QStringLiteral("Quick brush size stays bound to the active retouch brush"));
  }

  const auto capture_root = qEnvironmentVariable("PHOTO_RETOUCH_TOOL_HUD_OUTPUT");
  if (!capture_root.isEmpty() && !QDir().mkpath(capture_root)) {
    return fail(QStringLiteral("Create requested Retouch HUD capture directory"));
  }
  if (!capture_root.isEmpty() && QGuiApplication::platformName() == QStringLiteral("cocoa") &&
      !window->isExposed()) {
    return fail(QStringLiteral("Expose the native Retouch HUD window before capturing Cocoa frames"));
  }
  for (const double scale : {1.0, 2.0}) {
    app_shell->setProperty("interfaceScale", scale);
    for (const int base_width : {300, 520}) {
      window->setWidth(base_width);
      window->setHeight(820);
      settle();
      if (hud->width() > window->width() - 22 || hud->width() <= 0) {
        return fail(QStringLiteral("Retouch HUD width must remain inside the 300/520 responsive fixture"));
      }
      if (hud->height() > window->height() - 20) {
        return fail(QStringLiteral("Retouch HUD should stay within the visible viewport height"));
      }
      QString bounds_failure;
      if (!descendants_fit(hud, hud, &bounds_failure)) {
        return fail(QStringLiteral("Retouch HUD responsive bounds: ") + bounds_failure);
      }
      auto* mode_grid = find_visual(hud, QStringLiteral("retouchHudModeGrid"));
      auto* scroll = find_visual(hud, QStringLiteral("retouchHudScroll"));
      const auto expected_columns = hud->property("modeGridColumns").toInt();
      if (mode_grid == nullptr || scroll == nullptr || mode_grid->property("columns").toInt() != expected_columns) {
        return fail(QStringLiteral("Retouch modes should reflow from four columns to two within the HUD"));
      }
      if (!item_inside(remove, hud) || !item_inside(heal, hud) || !item_inside(patch, hud) ||
          !item_inside(clone, hud)) {
        return fail(QStringLiteral("Retouch mode controls should remain on-screen above the scroll region"));
      }
      if (!readable_button(remove) || !readable_button(heal) || !readable_button(patch) ||
          !readable_button(clone) || !readable_button(before) || !readable_button(after) ||
          !readable_button(add)) {
        return fail(QStringLiteral("Retouch labels and leading icons must fit at the tested width and scale"));
      }
      if (!capture_root.isEmpty()) {
        const auto path = QDir(capture_root).filePath(
            QStringLiteral("retouch-%1-scale%2.png").arg(base_width).arg(qRound(scale)));
        if (!capture_window(window, path)) return fail(QStringLiteral("Capture the requested Retouch HUD frame"));
      }
      const auto scroll_height = scroll->height();
      const auto scroll_content_height = scroll->property("contentHeight").toDouble();
      scroll->setProperty("contentY", qMax(0.0, scroll_content_height - scroll_height));
      settle();
      if (!item_inside(before, hud) || !item_inside(after, hud) ||
          !readable_button(before) || !readable_button(after)) {
        return fail(QStringLiteral("Scrolling should reveal full, readable Before and After controls inside the HUD"));
      }
      if (scale == 2.0 && !capture_root.isEmpty()) {
        const auto path = QDir(capture_root).filePath(
            QStringLiteral("retouch-%1-scale2-bottom.png").arg(base_width));
        if (!capture_window(window, path)) return fail(QStringLiteral("Capture the bottom of the scaled Retouch HUD"));
      }
      controller->setProperty("retouchBeforeAfterEnabled", false);
      if (!click(window, before) || !controller->property("retouchBeforeAfterEnabled").toBool() ||
          !click(window, after) || controller->property("retouchBeforeAfterEnabled").toBool()) {
        return fail(QStringLiteral("Scrolled Before/After controls should remain pointer-accessible"));
      }
      scroll->setProperty("contentY", 0);
      settle();
    }
  }

  const auto capture_variant = [&](const QString& mode, const QString& label) {
    controller->setProperty("activeMaskRetouchMode", mode);
    settle();
    const auto guidance = find_visual(hud, QStringLiteral("retouchHudGuidanceLabel"));
    const QString expected_guidance = mode == QStringLiteral("heal")
        ? QStringLiteral("blemish or seam")
        : mode == QStringLiteral("patch")
            ? QStringLiteral("repair area")
            : QStringLiteral("Pick a source point first");
    if (guidance == nullptr || guidance->property("wrapMode").toInt() == 0 ||
        guidance->property("text").toString() != hud->property("guidanceText").toString() ||
        !guidance->property("text").toString().contains(expected_guidance) ||
        guidance->height() + 1.0 < guidance->implicitHeight()) {
      return false;
    }
    if (mode == QStringLiteral("clone") && !readable_button(source))
      return false;
    if (!capture_root.isEmpty()) {
      const auto path = QDir(capture_root).filePath(QStringLiteral("retouch-mode-%1-scale2.png").arg(label));
      if (!capture_window(window, path)) return false;
    }
    return true;
  };
  app_shell->setProperty("interfaceScale", 2.0);
  window->setWidth(520);
  window->setHeight(820);
  for (const auto& mode : {std::pair<QString, QString>{QStringLiteral("heal"), QStringLiteral("heal")},
                           {QStringLiteral("patch"), QStringLiteral("patch")},
                           {QStringLiteral("clone"), QStringLiteral("clone")}}) {
    if (!capture_variant(mode.first, mode.second))
      return fail(QStringLiteral("Capture the Heal, Patch, and Clone Retouch layouts at scale 2"));
  }

  controller->setProperty("activeMaskRetouchMode", QStringLiteral("remove"));
  if (!QMetaObject::invokeMethod(secondary_mode_menu, "openMenu"))
    return fail(QStringLiteral("Open the Other tools menu from the live Retouch HUD"));
  settle();
  auto* dust_menu_item = find_visual(window->contentItem(), QStringLiteral("retouchHudModeMenuButtonPopupItem0"));
  if (dust_menu_item == nullptr || !click(window, dust_menu_item) ||
      controller->property("activeMaskRetouchMode").toString() != QStringLiteral("dust") ||
      active_mode_label->property("text").toString() != QStringLiteral("Dust Cleanup") ||
      !active_mode_label->isVisible() ||
      !find_visual(hud, QStringLiteral("retouchHudGuidanceLabel"))->property("text").toString().contains(QStringLiteral("small spots"))) {
    return fail(QStringLiteral("Secondary Dust Cleanup mode keeps an explicit name and next step"));
  }
  app_shell->setProperty("interfaceScale", 2.0);
  controller->setProperty("activeMaskRetouchMode", QStringLiteral("remove"));
  settle();
  if (!click(window, preset_button)) return fail(QStringLiteral("Open the Retouch brush presets"));
  settle();
  auto* preset_popup = window->findChild<QObject*>(QStringLiteral("brushPresetPopup"));
  auto* builtin_preset = find_visual(window->contentItem(), QStringLiteral("brushBuiltinPreset0"));
  if (preset_popup == nullptr || builtin_preset == nullptr ||
      !preset_popup->property("visible").toBool())
    return fail(QStringLiteral("Expose the live preset popup and built-in brush choices"));
  if (!capture_root.isEmpty()) {
    const auto path = QDir(capture_root).filePath(QStringLiteral("retouch-presets-scale2.png"));
    if (!capture_window(window, path)) return fail(QStringLiteral("Capture the Retouch presets popup"));
  }
  if (!click(window, builtin_preset) ||
      qAbs(controller->property("activeMaskBrushHardness").toDouble() - 1.0) > 0.005) {
    return fail(QStringLiteral("Choose a built-in preset through the existing brush controller binding"));
  }

  QFile workspace_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  QFile viewport_source(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/StudioImageViewport.qml"));
  if (!workspace_source.open(QIODevice::ReadOnly) || !viewport_source.open(QIODevice::ReadOnly))
    return fail(QStringLiteral("Read the production clone-source picking helpers"));
  const auto workspace_text = QString::fromUtf8(workspace_source.readAll());
  const auto viewport_text = QString::fromUtf8(viewport_source.readAll());
  const auto available_fn = qml_function(workspace_text, QStringLiteral("cloneSourcePickAvailable"));
  const auto toggle_fn = qml_function(workspace_text, QStringLiteral("toggleCloneSourcePick"));
  const auto cancel_fn = qml_function(workspace_text, QStringLiteral("cancelCloneSourcePick"));
  const auto complete_fn = qml_function(workspace_text, QStringLiteral("completeCloneSourcePick"));
  const auto pointer_fn = qml_function(viewport_text, QStringLiteral("handleMaskPointerPressed"));
  if (available_fn.isEmpty() || toggle_fn.isEmpty() || cancel_fn.isEmpty() ||
      complete_fn.isEmpty() || pointer_fn.isEmpty())
    return fail(QStringLiteral("Extract the production clone-source arm and viewport pointer functions"));

  QByteArray pointer_qml = QByteArrayLiteral(R"qml(
import QtQuick
Item {
    id: root
    objectName: "cloneSourcePickHarness"
    width: 240; height: 180
    property string page: "edit"
    property bool workspaceActive: true
    property string editTool: "retouch"
    property bool cloneSourcePickArmed: false
    property var editorPreview: controller
    readonly property var editorPreviewController: editorPreview
    property bool objectPickArmed: false
    function pixelExactToolReady() { return false }
    QtObject {
        id: controller
        objectName: "cloneSourcePickController"
        property bool hasActiveMask: true
        property string activeMaskToolRole: "retouch"
        property string activeMaskRetouchMode: "clone"
        property bool activeMaskCommitted: false
        property bool activeMaskLocked: false
        property bool inpaintProgressActive: false
        property bool compareMode: false
        property bool activeMaskCloneSourceSet: true
        property real sourceX: 0.1
        property real sourceY: 0.2
        property int sourcePickCount: 0
        property int strokeStartCount: 0
        property int strokePointCount: 0
        function setActiveMaskCloneSource(x, y) {
            sourceX = x; sourceY = y; activeMaskCloneSourceSet = true; sourcePickCount++
        }
        function beginActiveBrushSourceStroke(operation) { strokeStartCount++ }
        function addBrushSourcePointWithPressure(x, y, pressure) { strokePointCount++ }
    }
%1%2%3%4
    Item {
        id: viewport
        objectName: "cloneSourcePickViewport"
        width: 100; height: 100
        property var workspaceContext: root
        property var previewContext: controller
        property bool previewContentVisible: true
        property bool spacePanActive: false
        property bool masked: true
        property bool pixelGridVisible: false
        property string maskGestureId: ""
        property string maskGestureKind: ""
        readonly property bool retouchBrushActive: true
        readonly property bool canvasBrushActive: true
        function maskPointerActive() { return true }
        function normalizedPointForViewport(x, y) { return {valid: true, x: x / 100, y: y / 100} }
        function currentMaskIdentity() { return "clone-mask" }
        function pixelGridVisibleForTest() { return false }
%5
    }
    function press(x, y) { return viewport.handleMaskPointerPressed({x: x, y: y, modifiers: Qt.NoModifier}) }
}
)qml");
  const auto first_marker = pointer_qml.indexOf("%1%2%3%4");
  pointer_qml.replace(first_marker, QByteArrayLiteral("%1%2%3%4").size(),
                      available_fn.toUtf8() + '\n' + toggle_fn.toUtf8() + '\n' +
                      cancel_fn.toUtf8() + '\n' + complete_fn.toUtf8());
  const auto viewport_marker = pointer_qml.indexOf("%5");
  pointer_qml.replace(viewport_marker, 2, pointer_fn.toUtf8());
  QQmlComponent pointer_component(&engine);
  pointer_component.setData(pointer_qml, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/retouch_tool_hud_ui_pointer_fixture.qml")));
  std::unique_ptr<QObject> pointer_object(pointer_component.create());
  if (!pointer_object) return fail(pointer_component.errorString());
  auto* pointer_root = pointer_object.get();
  auto* pointer_controller = pointer_root->findChild<QObject*>(QStringLiteral("cloneSourcePickController"));
  QVariant method_result;
  if (pointer_controller == nullptr ||
      !QMetaObject::invokeMethod(pointer_root, "toggleCloneSourcePick", Q_RETURN_ARG(QVariant, method_result)) ||
      !method_result.toBool() || !pointer_root->property("cloneSourcePickArmed").toBool())
    return fail(QStringLiteral("Arm an explicit Clone source pick from the real workspace helper"));
  const auto press_pointer = [&](double x, double y) {
    return QMetaObject::invokeMethod(pointer_root, "press", Q_RETURN_ARG(QVariant, method_result),
                                     Q_ARG(QVariant, QVariant(x)), Q_ARG(QVariant, QVariant(y))) &&
           method_result.toBool();
  };
  if (!press_pointer(25, 40) || pointer_root->property("cloneSourcePickArmed").toBool() ||
      pointer_controller->property("sourcePickCount").toInt() != 1 ||
      qAbs(pointer_controller->property("sourceX").toDouble() - 0.25) > 0.005 ||
      qAbs(pointer_controller->property("sourceY").toDouble() - 0.4) > 0.005 ||
      pointer_controller->property("strokeStartCount").toInt() != 0 ||
      pointer_controller->property("strokePointCount").toInt() != 0)
    return fail(QStringLiteral("The armed canvas click picks a source once and never paints"));
  if (!QMetaObject::invokeMethod(pointer_root, "toggleCloneSourcePick", Q_RETURN_ARG(QVariant, method_result)) ||
      !method_result.toBool() || !press_pointer(75, 80) ||
      pointer_controller->property("sourcePickCount").toInt() != 2 ||
      pointer_controller->property("strokeStartCount").toInt() != 0)
    return fail(QStringLiteral("The explicit source picker can be armed again to repick"));
  if (!press_pointer(20, 30) || pointer_controller->property("strokeStartCount").toInt() != 1 ||
      pointer_controller->property("strokePointCount").toInt() != 1)
    return fail(QStringLiteral("The next ordinary Clone click paints after source selection"));
  if (!QMetaObject::invokeMethod(pointer_root, "toggleCloneSourcePick", Q_RETURN_ARG(QVariant, method_result)) ||
      !method_result.toBool() ||
      !QMetaObject::invokeMethod(pointer_root, "cancelCloneSourcePick", Q_RETURN_ARG(QVariant, method_result)) ||
      !method_result.toBool() || pointer_root->property("cloneSourcePickArmed").toBool() ||
      pointer_controller->property("sourcePickCount").toInt() != 2)
    return fail(QStringLiteral("Canceling an armed source pick leaves the selected source untouched"));

  std::cout << "Retouch HUD actions, mode labels, brush presets, clone source pick/no-paint, and 300/520 width-scale matrix passed\n";
  return EXIT_SUCCESS;
}
