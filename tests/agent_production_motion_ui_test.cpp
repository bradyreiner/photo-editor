#include "agent_edit_controller.h"

#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <cmath>

namespace {

constexpr auto kFixtureEnvironment = "PHOTO_EDITOR_AGENT_PRODUCTION_MOTION_FIXTURE";
constexpr auto kTraceEnvironment = "PHOTO_EDITOR_AGENT_MOTION_TRACE_PATH";

const char kProductionMotionHostQml[] = R"QML(
import QtQuick
import QtQuick.Window
import QtQuick.Controls.Basic
import "../apps/desktop/qml/components"

Window {
    id: host
    objectName: "agentProductionMotionHost"
    width: 1366
    height: 768
    visible: true
    color: "#111820"
    title: "Agent production motion test"

    property var controllerObject: productionMotionController
    property var catalogObject: productionMotionCatalog
    property var characterObject: null
    property string observedActStage: ""
    property string observedActStatus: ""
    property string observedActAtUtc: ""
    property string observedActKey: ""
    property string observedActControl: ""
    property real observedActX: -1
    property real observedActY: -1
    property real observedToolContactError: -1
    property var motionTrace: []
    property var resolverTrace: []
    property var actResolverTrace: []
    property int inspectorSelectionCount: 0
    property int sliderValueChangeCount: 0
    property int sliderPressedChangeCount: 0
    property int sliderFocusChangeCount: 0
    property int inspectorSelectionChangeCount: 0
    property int inspectorFocusChangeCount: 0

    function toolContactError() {
        if (!host.characterObject)
            return -1
        const tip = host.characterObject.toolContactPoint
        const mapped = host.characterObject.mapToItem(overlay, tip.x, tip.y)
        const target = overlay.currentTouchTarget || ({})
        return Math.hypot(mapped.x - Number(target.x || 0), mapped.y - Number(target.y || 0))
    }

    Item {
        id: workspacePage
        objectName: "productionMotionWorkspacePage"
        anchors.fill: parent

        Rectangle {
            id: inspectorPanel
            objectName: "editInspectorPanel"
            x: workspacePage.width - width - 24
            y: 56
            width: 360
            height: workspacePage.height - 112
            color: "#1b242e"
            border.color: "#52616f"
            border.width: 1
            clip: true

            EditInspectorTabButton {
                id: adjustInspectorTabButton
                objectName: "adjustInspectorTabButton"
                x: 20
                y: 20
                label: "Adjust"
                tabKey: "adjust"
                selected: true
                property point agentContactPoint: Qt.point(width * 0.5, height * 0.5)
                property rect agentSurfaceRect: Qt.rect(0, height, width, 0)
                onSelectedRequested: host.inspectorSelectionCount += 1
            }

            InspectorSlider {
                id: exposureSlider
                objectName: "exposureSlider"
                x: 20
                y: adjustInspectorTabButton.y + adjustInspectorTabButton.height + 24
                width: parent.width - 40
                label: "Exposure"
                from: -5
                to: 5
                stepSize: 0.01
                value: 0
                valueText: "0.00 EV"
                showResetButton: true
            }
        }
    }

    AgentWorkspaceTouchResolver {
        id: touchResolver
    }

    AgentWorkspaceOverlay {
        id: overlay
        objectName: "agentWorkspaceOverlay"
        anchors.fill: parent
        visible: true
        enabled: true
        z: 20
        agentEditController: host.controllerObject
        catalogModel: host.catalogObject
        targetAssetIds: ["production-motion-asset"]
        prompt: "Make this photo slightly brighter."
        inspectorWidth: 360
        filmstripHeight: 0
        applyInProgress: false
        reducedMotion: false
        targetResolver: function(event, parameterKey) {
            const mapped = touchResolver.resolve(
                        event, parameterKey, workspacePage, overlay, null, host)
            host.resolverTrace = host.resolverTrace.concat([{
                stage: String((event || ({})).stage || ""),
                status: String((event || ({})).status || ""),
                atUtc: String((event || ({})).atUtc || ""),
                uiAnchor: String((event || ({})).uiAnchor || ""),
                parameterKey: String(parameterKey || ""),
                adjustmentKeys: ((event || ({})).evidence || ({})).adjustmentKeys || [],
                mapped: Boolean(mapped),
                controlName: mapped ? String(mapped.controlName || "") : "",
                x: mapped ? Number(mapped.x) : -1,
                y: mapped ? Number(mapped.y) : -1,
                workspaceVisible: workspacePage.visible,
                inspectorVisible: inspectorPanel.visible,
                sliderVisible: exposureSlider.visible,
                sliderEnabled: exposureSlider.enabled
            }]).slice(-128)
            if (String((event || ({})).stage || "") === "act" &&
                    String((event || ({})).status || "") === "complete") {
                host.actResolverTrace = host.actResolverTrace.concat([{
                    atUtc: String(event.atUtc || ""),
                    parameterKey: String(parameterKey || ""),
                    adjustmentKeys: ((event.evidence || ({})).adjustmentKeys || []),
                    mapped: Boolean(mapped),
                    controlName: mapped ? String(mapped.controlName || "") : "",
                    x: mapped ? Number(mapped.x) : -1,
                    y: mapped ? Number(mapped.y) : -1,
                    workspaceVisible: workspacePage.visible,
                    inspectorVisible: inspectorPanel.visible,
                    sliderVisible: exposureSlider.visible,
                    sliderEnabled: exposureSlider.enabled
                }]).slice(-32)
            }
            if (String((event || ({})).stage || "") === "act" &&
                    String((event || ({})).status || "") === "complete") {
                host.observedActStage = String(event.stage || "")
                host.observedActStatus = String(event.status || "")
                host.observedActAtUtc = String(event.atUtc || "")
                host.observedActKey = String(parameterKey || "")
                host.observedActControl = mapped ? String(mapped.controlName || "") : ""
                host.observedActX = mapped ? Number(mapped.x) : -1
                host.observedActY = mapped ? Number(mapped.y) : -1
            }
            return mapped
        }
    }

    Timer {
        interval: 16
        repeat: true
        running: true
        onTriggered: {
            if (String(overlay.motionEvent.stage || "") !== "act")
                return
            host.motionTrace = host.motionTrace.concat([{
                atUtc: String(overlay.motionEvent.atUtc || ""),
                key: String(overlay.activeParameterKey || ""),
                controlName: String(overlay.currentTouchTarget.controlName || ""),
                robotX: Number(overlay.robotX),
                robotY: Number(overlay.robotY),
                toolContactError: host.toolContactError(),
                locomotion: String(overlay.locomotion || ""),
                touchingControl: Boolean(overlay.touchingControl),
                targetX: Number(overlay.currentTouchTarget.x || 0),
                targetY: Number(overlay.currentTouchTarget.y || 0),
                tool: String(overlay.deployedTool || ""),
                reducedMotion: Boolean(overlay.reducedMotion)
            }]).slice(-2048)
            host.observedToolContactError = host.toolContactError()
        }
    }

    property int exposureValueDragCount: 0
    property int exposureResetCount: 0
    property int reducedMotionCoordinateChanges: 0

    Connections {
        target: exposureSlider
        function onValueDragged() { host.exposureValueDragCount += 1 }
        function onResetRequested() { host.exposureResetCount += 1 }
        function onValueChanged() { host.sliderValueChangeCount += 1 }
        function onPressedChanged() { host.sliderPressedChangeCount += 1 }
        function onActiveFocusChanged() { host.sliderFocusChangeCount += 1 }
    }

    Connections {
        target: adjustInspectorTabButton
        function onSelectedChanged() { host.inspectorSelectionChangeCount += 1 }
        function onActiveFocusChanged() { host.inspectorFocusChangeCount += 1 }
    }

    Connections {
        target: overlay
        function onRobotXChanged() {
            if (overlay.reducedMotion)
                host.reducedMotionCoordinateChanges += 1
        }
        function onRobotYChanged() {
            if (overlay.reducedMotion)
                host.reducedMotionCoordinateChanges += 1
        }
    }
}
)QML";

photo_editor::desktop::CatalogAssetItem make_motion_asset(const QString& source_path) {
  photo_editor::desktop::EditRecipeState neutral;
  neutral.exposure = 0.0;
  neutral.contrast = 0.0;
  neutral.saturation = 0.0;
  neutral.output_color_space = QStringLiteral("Display P3");

  photo_editor::desktop::CatalogAssetItem asset;
  asset.asset_id = QStringLiteral("production-motion-asset");
  asset.camera_label = QStringLiteral("Isolated motion fixture");
  asset.file_path = source_path;
  asset.media_type = QStringLiteral("photo");
  asset.adjustment_patch_json =
      photo_editor::desktop::canonical_asset_bound_adjustment_patch_json(
          photo_editor::desktop::serialize_edit_recipe_json(neutral));
  asset.online = true;
  return asset;
}

bool write_motion_fixture_image(const QString& path) {
  QImage image(48, 32, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(54 + x * 2, 70 + y * 2, 96 + x));
    }
  }
  return image.save(path, "PNG");
}

QString source_root() {
#ifdef PHOTO_EDITOR_SOURCE_DIR
  return QString::fromUtf8(PHOTO_EDITOR_SOURCE_DIR);
#else
  return {};
#endif
}

QVariantList controller_events(const photo_editor::desktop::AgentEditController& controller) {
  return controller.runEvents();
}

QVariantMap terminal_act_event(const QVariantList& events) {
  for (const auto& event_value : events) {
    const auto event = event_value.toMap();
    if (event.value(QStringLiteral("stage")).toString() == QStringLiteral("act") &&
        event.value(QStringLiteral("status")).toString() == QStringLiteral("complete")) {
      return event;
    }
  }
  return {};
}

QStringList numeric_patch_keys(const QVariantMap& patch) {
  QStringList keys;
  for (auto group = patch.cbegin(); group != patch.cend(); ++group) {
    const auto parameters = group.value().toMap();
    for (auto parameter = parameters.cbegin(); parameter != parameters.cend(); ++parameter) {
      if (parameter.value().canConvert<double>() && !keys.contains(parameter.key()))
        keys.append(parameter.key());
    }
  }
  return keys;
}

bool capture_window_frame(QQuickWindow* window,
                          const QString& output_dir,
                          const QString& file_name,
                          const QVariantMap& receipt,
                          const QString& request_id,
                          QJsonArray* frame_manifest) {
  if (!window || !frame_manifest)
    return false;
  window->requestUpdate();
  QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
  auto image = window->grabWindow();
  const auto file_path = QDir(output_dir).filePath(file_name);
  const auto receipt_at_utc = receipt.value(QStringLiteral("atUtc")).toString();
  const auto stage = receipt.value(QStringLiteral("stage")).toString();
  if (image.isNull())
    return false;
  image.setText(QStringLiteral("fixtureReceipt"), QStringLiteral("true"));
  image.setText(QStringLiteral("source"), QStringLiteral("AgentEditController.runEvents"));
  image.setText(QStringLiteral("modelFixture"), QStringLiteral("rulebook-deterministic"));
  image.setText(QStringLiteral("requestId"), request_id);
  image.setText(QStringLiteral("receiptStage"), stage);
  image.setText(QStringLiteral("receiptAtUtc"), receipt_at_utc);
  image.setText(QStringLiteral("qpaPlatform"), QGuiApplication::platformName());
  if (!image.save(file_path, "PNG"))
    return false;
  frame_manifest->append(QJsonObject{
      {QStringLiteral("file"), file_name},
      {QStringLiteral("path"), file_path},
      {QStringLiteral("windowWidth"), window->width()},
      {QStringLiteral("windowHeight"), window->height()},
      {QStringLiteral("pixelWidth"), image.width()},
      {QStringLiteral("pixelHeight"), image.height()},
      {QStringLiteral("devicePixelRatio"), window->devicePixelRatio()},
      {QStringLiteral("qpaPlatform"), QGuiApplication::platformName()},
      {QStringLiteral("fixtureReceipt"), true},
      {QStringLiteral("source"), QStringLiteral("AgentEditController.runEvents")},
      {QStringLiteral("modelFixture"), QStringLiteral("rulebook-deterministic")},
      {QStringLiteral("requestId"), request_id},
      {QStringLiteral("receiptStage"), stage},
      {QStringLiteral("receiptAtUtc"), receipt_at_utc},
  });
  return true;
}

bool write_motion_trace(const QString& path,
                        const QVariantMap& receipt,
                        const QVariantList& trace,
                        const QString& request_id,
                        const QJsonArray& frames) {
  if (path.trimmed().isEmpty())
    return false;
  QFile output(path);
  if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return false;
  const QJsonObject header{
      {QStringLiteral("fixtureReceipt"), true},
      {QStringLiteral("source"), QStringLiteral("AgentEditController.runEvents")},
      {QStringLiteral("modelFixture"), QStringLiteral("rulebook-deterministic")},
      {QStringLiteral("requestId"), request_id},
      {QStringLiteral("qpaPlatform"), QGuiApplication::platformName()},
      {QStringLiteral("stage"), receipt.value(QStringLiteral("stage")).toString()},
      {QStringLiteral("status"), receipt.value(QStringLiteral("status")).toString()},
      {QStringLiteral("atUtc"), receipt.value(QStringLiteral("atUtc")).toString()},
      {QStringLiteral("evidence"), QJsonObject::fromVariantMap(receipt.value(QStringLiteral("evidence")).toMap())},
      {QStringLiteral("fixtureRoot"), QFileInfo(path).absolutePath()},
      {QStringLiteral("frames"), frames},
  };
  const auto header_line = QJsonDocument(header).toJson(QJsonDocument::Compact) + '\n';
  if (output.write(header_line) != header_line.size())
    return false;
  for (const auto& row : trace) {
    const auto line = QJsonDocument(QJsonObject::fromVariantMap(row.toMap()))
                          .toJson(QJsonDocument::Compact) + '\n';
    if (output.write(line) != line.size())
      return false;
  }
  return output.flush();
}

class AgentProductionMotionUiTest final : public QObject {
  Q_OBJECT

 private slots:
  void productionReceiptRoutesToMappedInspectorAndTracksResize() {
    const auto isolated_root = qEnvironmentVariable(kFixtureEnvironment);
    if (isolated_root.trimmed().isEmpty())
      QSKIP("Set PHOTO_EDITOR_AGENT_PRODUCTION_MOTION_FIXTURE to an isolated writable fixture directory.");
    if (!QDir::isAbsolutePath(isolated_root))
      QFAIL("PHOTO_EDITOR_AGENT_PRODUCTION_MOTION_FIXTURE must be an absolute caller-owned evidence directory.");
    if (source_root().isEmpty())
      QFAIL("The native test target must define PHOTO_EDITOR_SOURCE_DIR for loading the production QML sources.");
    if (!QDir().mkpath(isolated_root))
      QFAIL("Could not create the isolated production-motion fixture directory.");

    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName(QStringLiteral("LooksyIsolatedTests"));
    QCoreApplication::setApplicationName(
        QStringLiteral("AgentProductionMotionUiTest-%1").arg(QCoreApplication::applicationPid()));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    const auto settings_root = QDir(isolated_root).filePath(QStringLiteral("settings"));
    QVERIFY(QDir().mkpath(settings_root));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_root);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, settings_root);

    QTemporaryDir fixture(QDir(isolated_root).filePath(QStringLiteral("motion-XXXXXX")));
    QVERIFY2(fixture.isValid(), "Could not allocate isolated controller and image fixture data.");
    const auto app_data_root = fixture.filePath(QStringLiteral("app-data"));
    const auto cache_root = fixture.filePath(QStringLiteral("cache"));
    const auto observability_root = fixture.filePath(QStringLiteral("observability"));
    QVERIFY(QDir().mkpath(app_data_root));
    QVERIFY(QDir().mkpath(cache_root));
    QVERIFY(QDir().mkpath(observability_root));
    QVERIFY(qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data_root.toUtf8()));
    QVERIFY(qputenv("PHOTO_EDITOR_APP_DATA_ROOT", app_data_root.toUtf8()));
    QVERIFY(qputenv("PHOTO_EDITOR_CACHE_DIR", cache_root.toUtf8()));
    QVERIFY(qputenv("PHOTO_EDITOR_OBSERVABILITY_RUN_DIR", observability_root.toUtf8()));
    const auto image_path = fixture.filePath(QStringLiteral("source.png"));
    QVERIFY(write_motion_fixture_image(image_path));

    photo_editor::desktop::CatalogAssetModel catalog;
    catalog.replaceAssetsForTesting({make_motion_asset(image_path)});
    catalog.selectIndex(0);
    catalog.clearSelection();
    catalog.toggleSelectionIndex(0);

    photo_editor::desktop::AgentEditController controller;
    controller.setCatalogModel(&catalog);
    controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("productionMotionController"), &controller);
    engine.rootContext()->setContextProperty(QStringLiteral("productionMotionCatalog"), &catalog);
    const auto host_url = QUrl::fromLocalFile(
        QDir(source_root()).filePath(QStringLiteral("tests/agent_production_motion_ui_test.qml")));
    QQmlComponent host_component(&engine);
    host_component.setData(QByteArray(kProductionMotionHostQml), host_url);
    QVERIFY2(host_component.isReady(), qPrintable(host_component.errorString()));
    QObject* host_object = host_component.create();
    QVERIFY2(host_object, qPrintable(host_component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(host_object);
    QVERIFY(window);
    window->show();
    QTRY_VERIFY_WITH_TIMEOUT(window->isExposed(), 5000);

    QObject* mapped_control = host_object->findChild<QObject*>(QStringLiteral("exposureSlider"));
    QObject* inspector_tab = host_object->findChild<QObject*>(QStringLiteral("adjustInspectorTabButton"));
    QObject* reset_button = host_object->findChild<QObject*>(QStringLiteral("exposureSliderResetButton"));
    QObject* overlay = host_object->findChild<QObject*>(QStringLiteral("agentWorkspaceOverlay"));
    QObject* character = host_object->findChild<QObject*>(QStringLiteral("agentWorkspaceRoamingCharacter"));
    QObject* close_action = host_object->findChild<QObject*>(QStringLiteral("agentWorkspaceStatusCloseButton"));
    QVERIFY(mapped_control);
    QVERIFY(inspector_tab);
    QVERIFY(reset_button);
    QVERIFY(overlay);
    QVERIFY(character);
    host_object->setProperty("characterObject", QVariant::fromValue(character));
    QVERIFY(close_action);
    const auto original_slider_value = mapped_control->property("value");
    const auto original_slider_focus = mapped_control->property("activeFocus");
    const auto original_tab_selected = inspector_tab->property("selected");
    const auto original_tab_focus = inspector_tab->property("activeFocus");
    const auto original_window_focus = window->activeFocusItem();
    const auto original_reset_enabled = reset_button->property("enabled");
    const auto original_interaction_counts = QVariantMap{
        {QStringLiteral("sliderValue"), host_object->property("sliderValueChangeCount")},
        {QStringLiteral("sliderPressed"), host_object->property("sliderPressedChangeCount")},
        {QStringLiteral("sliderFocus"), host_object->property("sliderFocusChangeCount")},
        {QStringLiteral("tabSelection"), host_object->property("inspectorSelectionChangeCount")},
        {QStringLiteral("tabFocus"), host_object->property("inspectorFocusChangeCount")},
    };
    const auto original_recipe_patch = catalog.assetAt(0)
                                           .value(QStringLiteral("adjustmentPatchJson"))
                                           .toString();
    QVERIFY(reset_button->property("visible").toBool());

    QVariantMap completed_report;
    auto completion_count = 0;
    const auto completion_connection = QObject::connect(
        &controller,
        &photo_editor::desktop::AgentEditController::draftRunFinished,
        &controller,
        [&](const QString&, const QVariantMap& report) {
          completed_report = report;
          ++completion_count;
        });
    const auto start = controller.startDraftForScope(
        QStringLiteral("current-photo"),
        QStringLiteral("Make this photo slightly brighter."));
    QVERIFY2(start.value(QStringLiteral("accepted")).toBool(),
             qPrintable(start.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; "))));
    QVERIFY(!start.value(QStringLiteral("requestId")).toString().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(completion_count == 1, 45000);
    QObject::disconnect(completion_connection);

    QVERIFY2(completed_report.value(QStringLiteral("ok")).toBool(),
             qPrintable(completed_report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; "))));
    const auto receipt = terminal_act_event(controller_events(controller));
    QVERIFY2(!receipt.isEmpty(), "The real controller run must emit a terminal act receipt.");
    const auto event_keys = receipt.value(QStringLiteral("evidence")).toMap()
                                .value(QStringLiteral("adjustmentKeys")).toStringList();
    QVERIFY2(!event_keys.isEmpty(),
             "The production act receipt must identify accepted numeric recipe keys.");

    QStringList authored_keys;
    for (const auto& report_value : completed_report.value(QStringLiteral("assetReports")).toList()) {
      const auto report = report_value.toMap();
      if (report.value(QStringLiteral("assetId")).toString() ==
          QStringLiteral("production-motion-asset")) {
        authored_keys = numeric_patch_keys(report.value(QStringLiteral("finalRecipePatch")).toMap());
        break;
      }
    }
    QVERIFY2(!authored_keys.isEmpty(), "The controller report must contain the accepted recipe patch.");
    for (const auto& event_key : event_keys)
      QVERIFY2(authored_keys.contains(event_key), "The act receipt key must come from the accepted recipe patch.");

    const auto active_key_matches_receipt = [&]() {
      const auto key = overlay->property("activeParameterKey").toString();
      const auto motion_event = overlay->property("motionEvent").toMap();
      return event_keys.contains(key) &&
             motion_event.value(QStringLiteral("stage")).toString() == QStringLiteral("act") &&
             motion_event.value(QStringLiteral("status")).toString() == QStringLiteral("complete") &&
             motion_event.value(QStringLiteral("atUtc")).toString() ==
                 receipt.value(QStringLiteral("atUtc")).toString() &&
             overlay->property("currentTouchTarget").toMap()
                 .value(QStringLiteral("controlName")).toString() ==
                 QStringLiteral("exposureSlider");
    };
    QElapsedTimer mapping_wait;
    mapping_wait.start();
    while (!active_key_matches_receipt() &&
           mapping_wait.elapsed() < 20000) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
      QTest::qWait(20);
    }
    if (!active_key_matches_receipt()) {
      const auto failure_image_path = QStringLiteral("production-mapping-timeout-1366x768.png");
      QJsonArray failure_frames;
      const auto failure_capture = capture_window_frame(
          window, isolated_root, failure_image_path, receipt,
          start.value(QStringLiteral("requestId")).toString(), &failure_frames);
      const auto controller_event_list = controller_events(controller);
      const auto resolver_trace = host_object->property("resolverTrace").toList();
      const auto act_resolver_trace = host_object->property("actResolverTrace").toList();
      const auto event_list_json = QJsonValue::fromVariant(controller_event_list);
      const auto resolver_trace_json = QJsonValue::fromVariant(resolver_trace);
      const QVariantMap control_state{
          {QStringLiteral("sliderVisible"), mapped_control->property("visible")},
          {QStringLiteral("sliderEnabled"), mapped_control->property("enabled")},
          {QStringLiteral("sliderWidth"), mapped_control->property("width")},
          {QStringLiteral("sliderHeight"), mapped_control->property("height")},
          {QStringLiteral("sliderActiveFocus"), mapped_control->property("activeFocus")},
          {QStringLiteral("inspectorSelected"), inspector_tab->property("selected")},
      };
      const QVariantMap overlay_state{
          {QStringLiteral("visible"), overlay->property("visible")},
          {QStringLiteral("dismissed"), overlay->property("dismissed")},
          {QStringLiteral("activeEvent"), overlay->property("activeEvent")},
          {QStringLiteral("motionEvent"), overlay->property("motionEvent")},
          {QStringLiteral("activeParameterKey"), overlay->property("activeParameterKey")},
          {QStringLiteral("currentTouchTarget"), overlay->property("currentTouchTarget")},
          {QStringLiteral("motionTargets"), overlay->property("motionTargets")},
          {QStringLiteral("traveling"), overlay->property("traveling")},
          {QStringLiteral("locomotion"), overlay->property("locomotion")},
      };
      const QJsonObject diagnostics{
          {QStringLiteral("fixtureReceipt"), true},
          {QStringLiteral("source"), QStringLiteral("AgentEditController.runEvents")},
          {QStringLiteral("modelFixture"), QStringLiteral("rulebook-deterministic")},
          {QStringLiteral("qpaPlatform"), QGuiApplication::platformName()},
          {QStringLiteral("requestId"), start.value(QStringLiteral("requestId")).toString()},
          {QStringLiteral("terminalActReceipt"), QJsonObject::fromVariantMap(receipt)},
          {QStringLiteral("expectedControl"), QStringLiteral("exposureSlider")},
          {QStringLiteral("observedActStage"), host_object->property("observedActStage").toString()},
          {QStringLiteral("observedActStatus"), host_object->property("observedActStatus").toString()},
          {QStringLiteral("observedActKey"), host_object->property("observedActKey").toString()},
          {QStringLiteral("observedActControl"), host_object->property("observedActControl").toString()},
          {QStringLiteral("controllerEvents"), event_list_json},
          {QStringLiteral("resolverTrace"), resolver_trace_json},
          {QStringLiteral("actResolverTrace"), QJsonValue::fromVariant(act_resolver_trace)},
          {QStringLiteral("overlayState"), QJsonObject::fromVariantMap(overlay_state)},
          {QStringLiteral("controlState"), QJsonObject::fromVariantMap(control_state)},
          {QStringLiteral("capturedScreenshot"), failure_capture},
          {QStringLiteral("screenshotPath"), QDir(isolated_root).filePath(failure_image_path)},
      };
      const auto diagnostic_path = QDir(isolated_root).filePath(
          QStringLiteral("production-mapping-timeout.json"));
      QFile diagnostic_file(diagnostic_path);
      const auto diagnostic_bytes = QJsonDocument(diagnostics).toJson(QJsonDocument::Indented);
      const auto diagnostic_saved = diagnostic_file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                                   diagnostic_file.write(diagnostic_bytes) == diagnostic_bytes.size() &&
                                   diagnostic_file.flush();
      const auto timeout_trace_path = QDir(isolated_root).filePath(
          QStringLiteral("production-motion-timeout.jsonl"));
      const auto timeout_trace_saved = write_motion_trace(
          timeout_trace_path, receipt, host_object->property("motionTrace").toList(),
          start.value(QStringLiteral("requestId")).toString(), failure_frames);
      QFAIL(qPrintable(QStringLiteral(
          "Act receipt did not enter the exposureSlider route within %1 ms; resolver calls=%2, key=%3, screenshot=%4, diagnostics=%5 (saved=%6), trace=%7 (saved=%8)")
              .arg(mapping_wait.elapsed())
              .arg(resolver_trace.size())
              .arg(host_object->property("observedActKey").toString())
              .arg(QDir(isolated_root).filePath(failure_image_path))
              .arg(diagnostic_path)
              .arg(diagnostic_saved)
              .arg(timeout_trace_path)
              .arg(timeout_trace_saved)));
    }
    const auto observed_key = overlay->property("activeParameterKey").toString();
    QVERIFY2(event_keys.contains(observed_key), "The resolver must consume a key present in the real controller receipt.");
    QVERIFY2(authored_keys.contains(observed_key), "The mapped parameter must be authored in the accepted recipe.");
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("touchingControl").toBool(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        host_object->property("observedToolContactError").toDouble() <= 0.75,
        20000);

    QJsonArray frame_manifest;
    QVERIFY2(capture_window_frame(window, isolated_root,
                                  QStringLiteral("production-contact-1366x768.png"),
                                  receipt, start.value(QStringLiteral("requestId")).toString(),
                                  &frame_manifest),
             "The native real-receipt contact frame must be saved under the caller fixture directory.");
    const auto host_size_before = window->size();
    const auto target_before = overlay->property("currentTouchTarget").toMap();
    const auto event_count_before_resize = controller_events(controller).size();
    window->resize(1440, 900);
    QTRY_COMPARE_WITH_TIMEOUT(window->size(), QSize(1440, 900), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(overlay->property("currentTouchTarget").toMap()
                     .value(QStringLiteral("x")).toDouble() -
                 host_object->property("observedActX").toDouble()) < 0.75 &&
        std::abs(overlay->property("currentTouchTarget").toMap()
                     .value(QStringLiteral("y")).toDouble() -
                 host_object->property("observedActY").toDouble()) < 0.75,
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("touchingControl").toBool(), 20000);
    QTRY_VERIFY_WITH_TIMEOUT(
        host_object->property("observedToolContactError").toDouble() <= 0.75,
        20000);
    QVERIFY(window->size() != host_size_before);
    QVERIFY(target_before.value(QStringLiteral("x")).toDouble() !=
            overlay->property("currentTouchTarget").toMap().value(QStringLiteral("x")).toDouble());
    const auto resized_event_count = controller_events(controller).size();
    QCOMPARE(resized_event_count, event_count_before_resize);
    const auto resized_receipt = terminal_act_event(controller_events(controller));
    QCOMPARE(resized_receipt.value(QStringLiteral("atUtc")).toString(),
             receipt.value(QStringLiteral("atUtc")).toString());
    QCOMPARE(overlay->property("motionEvent").toMap().value(QStringLiteral("atUtc")).toString(),
             receipt.value(QStringLiteral("atUtc")).toString());
    QCOMPARE(controller_events(controller).size(), resized_event_count);
    QCOMPARE(mapped_control->property("value"), original_slider_value);
    QCOMPARE(mapped_control->property("pressed").toBool(), false);
    QCOMPARE(mapped_control->property("activeFocus"), original_slider_focus);
    QCOMPARE(inspector_tab->property("selected"), original_tab_selected);
    QCOMPARE(inspector_tab->property("activeFocus"), original_tab_focus);
    QCOMPARE(reset_button->property("enabled"), original_reset_enabled);
    QCOMPARE(host_object->property("exposureValueDragCount").toInt(), 0);
    QCOMPARE(host_object->property("exposureResetCount").toInt(), 0);
    QCOMPARE(host_object->property("inspectorSelectionCount").toInt(), 0);
    QCOMPARE(host_object->property("sliderValueChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderValue")));
    QCOMPARE(host_object->property("sliderPressedChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderPressed")));
    QCOMPARE(host_object->property("sliderFocusChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderFocus")));
    QCOMPARE(host_object->property("inspectorSelectionChangeCount"),
             original_interaction_counts.value(QStringLiteral("tabSelection")));
    QCOMPARE(host_object->property("inspectorFocusChangeCount"),
             original_interaction_counts.value(QStringLiteral("tabFocus")));
    QVERIFY(!window->mouseGrabberItem());
    QCOMPARE(window->activeFocusItem(), original_window_focus);
    QVERIFY2(capture_window_frame(window, isolated_root,
                                  QStringLiteral("production-reanchored-contact-1440x900.png"),
                                  receipt, start.value(QStringLiteral("requestId")).toString(),
                                  &frame_manifest),
             "The resized real-receipt contact frame must be saved under the caller fixture directory.");

    overlay->setProperty("reducedMotion", true);
    QTRY_VERIFY_WITH_TIMEOUT(!character->property("motionEnabled").toBool() &&
                             !overlay->property("traveling").toBool(), 5000);
    QVERIFY(controller.draftActive());
    QVERIFY(close_action->property("visible").toBool());
    QCOMPARE(overlay->property("touchingControl").toBool(), false);
    QCOMPARE(overlay->property("reachProgress").toDouble(), 0.0);
    const auto reduced_size_before = window->size();
    const auto reduced_target_before = overlay->property("currentTouchTarget").toMap();
    host_object->setProperty("reducedMotionCoordinateChanges", 0);
    window->resize(1540, 960);
    QTRY_COMPARE_WITH_TIMEOUT(window->size(), QSize(1540, 960), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        std::abs(overlay->property("currentTouchTarget").toMap()
                     .value(QStringLiteral("x")).toDouble() -
                 host_object->property("observedActX").toDouble()) < 0.75 &&
        std::abs(overlay->property("currentTouchTarget").toMap()
                     .value(QStringLiteral("y")).toDouble() -
                 host_object->property("observedActY").toDouble()) < 0.75,
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        host_object->property("reducedMotionCoordinateChanges").toInt() > 0,
        5000);
    QTRY_VERIFY_WITH_TIMEOUT(
        host_object->property("observedToolContactError").toDouble() <= 0.75,
        5000);
    QVERIFY(window->size() != reduced_size_before);
    QVERIFY(reduced_target_before.value(QStringLiteral("x")).toDouble() !=
            overlay->property("currentTouchTarget").toMap().value(QStringLiteral("x")).toDouble());
    QCOMPARE(overlay->property("motionEvent").toMap().value(QStringLiteral("atUtc")).toString(),
             receipt.value(QStringLiteral("atUtc")).toString());
    QCOMPARE(controller_events(controller).size(), resized_event_count);
    QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("traveling").toBool() &&
                             overlay->property("locomotion").toString() == QStringLiteral("idle"), 5000);
    const auto quiet_robot_x = overlay->property("robotX");
    const auto quiet_robot_y = overlay->property("robotY");
    const auto quiet_coordinate_change_count =
        host_object->property("reducedMotionCoordinateChanges").toInt();
    QTest::qWait(260);
    QCOMPARE(overlay->property("robotX"), quiet_robot_x);
    QCOMPARE(overlay->property("robotY"), quiet_robot_y);
    QCOMPARE(host_object->property("reducedMotionCoordinateChanges").toInt(),
             quiet_coordinate_change_count);
    QVERIFY(!overlay->property("traveling").toBool());
    QVERIFY(!character->property("motionEnabled").toBool());
    QVERIFY(close_action->property("visible").toBool());
    QVERIFY(controller.draftActive());
    QCOMPARE(mapped_control->property("value"), original_slider_value);
    QCOMPARE(mapped_control->property("pressed").toBool(), false);
    QCOMPARE(mapped_control->property("activeFocus"), original_slider_focus);
    QCOMPARE(inspector_tab->property("selected"), original_tab_selected);
    QCOMPARE(inspector_tab->property("activeFocus"), original_tab_focus);
    QCOMPARE(reset_button->property("enabled"), original_reset_enabled);
    QCOMPARE(host_object->property("exposureValueDragCount").toInt(), 0);
    QCOMPARE(host_object->property("exposureResetCount").toInt(), 0);
    QCOMPARE(host_object->property("inspectorSelectionCount").toInt(), 0);
    QCOMPARE(host_object->property("sliderValueChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderValue")));
    QCOMPARE(host_object->property("sliderPressedChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderPressed")));
    QCOMPARE(host_object->property("sliderFocusChangeCount"),
             original_interaction_counts.value(QStringLiteral("sliderFocus")));
    QCOMPARE(host_object->property("inspectorSelectionChangeCount"),
             original_interaction_counts.value(QStringLiteral("tabSelection")));
    QCOMPARE(host_object->property("inspectorFocusChangeCount"),
             original_interaction_counts.value(QStringLiteral("tabFocus")));
    QVERIFY(!window->mouseGrabberItem());
    QCOMPARE(window->activeFocusItem(), original_window_focus);
    QVERIFY2(capture_window_frame(window, isolated_root,
                                  QStringLiteral("production-reduced-motion-resized-1540x960.png"),
                                  receipt, start.value(QStringLiteral("requestId")).toString(),
                                  &frame_manifest),
             "The reduced-motion reanchor frame must be saved under the caller fixture directory.");

    const auto trace = host_object->property("motionTrace").toList();
    QVERIFY2(!trace.isEmpty(), "The native overlay should expose sampled motion from the real act receipt.");
    bool saw_climb = false;
    bool saw_touch = false;
    bool saw_exact_contact = false;
    for (const auto& trace_value : trace) {
      const auto row = trace_value.toMap();
      if (row.value(QStringLiteral("atUtc")).toString() != receipt.value(QStringLiteral("atUtc")).toString())
        continue;
      saw_climb = saw_climb || row.value(QStringLiteral("locomotion")).toString() == QStringLiteral("climb");
      saw_touch = saw_touch || row.value(QStringLiteral("touchingControl")).toBool();
      saw_exact_contact = saw_exact_contact ||
                          (row.value(QStringLiteral("touchingControl")).toBool() &&
                           row.value(QStringLiteral("toolContactError")).toDouble() <= 0.75);
    }
    QVERIFY2(saw_climb, "The act event must drive a real climb along the mapped inspector panel.");
    QVERIFY2(saw_touch, "The act event must reach contact at the mapped inspector control.");
    QVERIFY2(saw_exact_contact, "The deployed tool must meet the mapped inspector control anchor.");

    auto trace_path = qEnvironmentVariable(kTraceEnvironment).trimmed();
    if (trace_path.isEmpty())
      trace_path = QDir(isolated_root).filePath(QStringLiteral("production-motion.jsonl"));
    QVERIFY2(write_motion_trace(trace_path, receipt, trace,
                                start.value(QStringLiteral("requestId")).toString(),
                                frame_manifest),
             "The real-receipt motion trace must be flushed to the requested evidence path.");
    window->close();
    delete host_object;

    const auto rollback = controller.rollbackDraft(QStringLiteral("production_motion_test_cleanup"));
    QVERIFY2(rollback.value(QStringLiteral("ok")).toBool(),
             qPrintable(rollback.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; "))));
    QVERIFY(!controller.draftActive());
    QCOMPARE(catalog.assetAt(0).value(QStringLiteral("adjustmentPatchJson")).toString(),
             original_recipe_patch);
  }
};

}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  AgentProductionMotionUiTest test;
  return QTest::qExec(&test, argc, argv);
}

#include "agent_production_motion_ui_test.moc"
