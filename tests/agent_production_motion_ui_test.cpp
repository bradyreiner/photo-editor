#include "qml_style_policy.h"
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
#include <QScreen>
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
    color: "#181818"
    title: "Agent production motion test"

    property var controllerObject: productionMotionController
    property var catalogObject: productionMotionCatalog
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
            color: "#202020"
            border.color: "#404040"
            border.width: 0
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
            host.motionTrace = host.motionTrace.concat([{
                atUtc: String(overlay.activeEvent.atUtc || ""),
                stage: String(overlay.activeEvent.stage || ""),
                status: String(overlay.activeEvent.status || ""),
                running: Boolean(overlay.running),
                draftReady: Boolean(overlay.draftReady),
                reducedMotion: Boolean(overlay.reducedMotion)
            }]).slice(-2048)
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
  void productionReceiptUpdatesStatusWithoutTouchingInspector() {
    QTemporaryDir automatic_fixture;
    const auto requested_root = qEnvironmentVariable(kFixtureEnvironment).trimmed();
    QVERIFY2(!requested_root.isEmpty() || automatic_fixture.isValid(), "Could not allocate an isolated status fixture.");
    const auto isolated_root = requested_root.isEmpty() ? automatic_fixture.path() : requested_root;
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
    QVERIFY2(!character, "The editor must not instantiate the Agent character.");
    auto* rig = qobject_cast<QQuickItem*>(host_object->findChild<QObject*>(QStringLiteral("agentWorkspaceRoamingRobot")));
    QVERIFY(rig);
    QVERIFY(!rig->isVisible());
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

    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("draftReady").toBool(), 5000);
    QVERIFY(controller.draftActive());
    QVERIFY(!controller.running());
    QVERIFY(catalog.assetAt(0).value(QStringLiteral("adjustmentPatchJson")).toString() != original_recipe_patch);
    const auto event_count = controller_events(controller).size();
    QJsonArray frame_manifest;
    for (const auto size : {QSize(1366, 768), QSize(1440, 900), QSize(1540, 960)}) {
      const auto frame = window->frameMargins();
      const auto available = window->screen()->availableGeometry().size()
          - QSize(frame.left() + frame.right(), frame.top() + frame.bottom());
      const auto fitted_size = QGuiApplication::platformName() == QStringLiteral("offscreen")
          ? size : size.boundedTo(available);
      window->resize(fitted_size);
      QTRY_COMPARE_WITH_TIMEOUT(window->size(), fitted_size, 5000);
      QTest::qWait(100);
      QVERIFY(!rig->isVisible());
      const auto issues = qml_popup_style_violations(host_object);
      QVERIFY2(issues.isEmpty(), qPrintable(issues.join('\n')));
      QCOMPARE(controller_events(controller).size(), event_count);
      QCOMPARE(terminal_act_event(controller_events(controller)), receipt);
      QVERIFY2(capture_window_frame(window, isolated_root,
          QStringLiteral("production-optical-%1x%2.png").arg(window->width()).arg(window->height()),
          receipt, start.value(QStringLiteral("requestId")).toString(), &frame_manifest),
          "The actual controller's draft status must render at every supported size.");
    }
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
    overlay->setProperty("reducedMotion", true);
    QTest::qWait(300);
    for (auto* object : overlay->findChildren<QObject*>())
      if (object->property("motionEnabled").isValid())
        QVERIFY2(!object->property("motionEnabled").toBool(), "Editor optical motion must stop in reduced-motion mode.");
    QVERIFY(!rig->isVisible());
    QVERIFY(controller.draftActive());
    auto* draft_close = host_object->findChild<QObject*>(QStringLiteral("agentWorkspaceCloseButton"));
    QVERIFY(draft_close);
    QVERIFY(QMetaObject::invokeMethod(draft_close, "click"));
    QVERIFY(overlay->property("dismissed").toBool());
    QVERIFY(controller.draftActive());
    const auto trace = host_object->property("motionTrace").toList();
    QVERIFY2(!trace.isEmpty(), "The overlay must sample status from the real controller receipts.");
    auto trace_path = qEnvironmentVariable(kTraceEnvironment).trimmed();
    if (trace_path.isEmpty())
      trace_path = QDir(isolated_root).filePath(QStringLiteral("production-motion.jsonl"));
    QVERIFY2(write_motion_trace(trace_path, receipt, trace,
                                start.value(QStringLiteral("requestId")).toString(),
                                frame_manifest),
             "The real-receipt optical status trace must be flushed to the requested evidence path.");
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
