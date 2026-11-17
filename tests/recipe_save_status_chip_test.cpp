#include <QGuiApplication>
#include <QFile>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QThread>
#include <QTest>
#include <functional>
#include <iostream>

namespace {
int fail(int line) {
  std::cerr << "Recipe save/recovery UI check failed at line " << line << '\n';
  return 1;
}
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  const std::function<QQuickItem*(QQuickItem*, const QString&)> find_item =
      [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
    if (!parent) return nullptr;
    if (parent->objectName() == name) return parent;
    for (auto* child : parent->childItems())
      if (auto* found = find_item(child, name)) return found;
    return nullptr;
  };
  const auto click_item = [](QQuickItem* item) {
    if (!item || !item->window() || !item->isVisible() || !item->isEnabled()) return false;
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier, point.toPoint());
    return true;
  };
  QQmlComponent component(&engine, QUrl::fromLocalFile(
      QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/RecipeSaveStatusChip.qml")));
  std::unique_ptr<QObject> object(component.createWithInitialProperties({{"savePending", false}, {"saveError", ""}}));
  auto* chip = qobject_cast<QQuickItem*>(object.get());
  if (!chip) { std::cerr << component.errorString().toStdString(); return fail(__LINE__); }
  QQuickWindow window;
  window.resize(330, 100);
  window.setColor(QColor("#161b20"));
  chip->setParentItem(window.contentItem());
  chip->setPosition(QPointF(16, 30));
  chip->setWidth(18);
  window.show();
  const auto settle = [] {
    for (int i = 0; i < 10; ++i) {
      QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
      QThread::msleep(5);
    }
  };
  settle();
  if (chip->isVisible()) return fail(__LINE__);
  chip->setProperty("recoveryCount", 1);
  chip->setProperty("recoveryDetail", "IMG_2130.CR2 is preserved locally; other photos save normally.");
  settle();
  if (!chip->isVisible() || chip->property("text") != "1 photo needs recovery" ||
      chip->property("tone") != "warning" || chip->property("busy").toBool()) return fail(__LINE__);
  const auto recovery_output = qEnvironmentVariable("PHOTO_EDITOR_TEST_RECOVERY_STATUS_IMAGE");
  if (!recovery_output.isEmpty()) {
    chip->setPosition(QPointF(310, 60));
    if (!window.grabWindow().save(recovery_output)) return fail(__LINE__);
    chip->setPosition(QPointF(16, 30));
  }
  chip->setProperty("savePending", true);
  settle();
  if (chip->property("text") != "Saving edits" || !chip->property("busy").toBool()) return fail(__LINE__);
  chip->setProperty("recoveryCount", 0);
  chip->setProperty("savePending", false);
  settle();
  chip->setProperty("savePending", true);
  settle();
  if (!chip->isVisible() || chip->property("text") != "Saving edits" || !chip->property("busy").toBool()) return fail(__LINE__);
  auto* spinner = chip->findChild<QQuickItem*>("recipeSaveSpinner");
  auto* error_chip = chip->findChild<QQuickItem*>("recipeSaveError");
  if (!spinner || !error_chip || !spinner->isVisible() || error_chip->isVisible() ||
      chip->property("color").isValid()) return fail(__LINE__);
  const QString error = "Recovery checkpoint is unavailable. Existing recovery data was retained.";
  chip->setProperty("saveError", error);
  settle();
  if (chip->property("text") != "Edits not safely saved" || chip->property("tone") != "error" ||
      chip->property("busy").toBool() || chip->property("detailText") != error ||
      chip->property("tooltipText") != error) return fail(__LINE__);
  chip->setProperty("savePending", false);
  settle();
  if (!chip->isVisible()) return fail(__LINE__); // Error survives even without queued work.
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_STATUS_IMAGE");
  if (!output.isEmpty() && !window.grabWindow().save(output)) return fail(__LINE__);
  chip->setProperty("saveError", "");
  settle();
  if (chip->isVisible()) return fail(__LINE__);
  chip->setParentItem(nullptr);

  // The recovery action opens a readable review and resolves outside the
  // repeater delegate's handler, keeping the archived-copy receipt visible.
  chip->setParentItem(window.contentItem());
  window.resize(650, 560);
  chip->setPosition(QPointF(630, 520));
  chip->setProperty("recoveryCount", 1);
  chip->setProperty("recoveryEntries", QVariantList{QVariantMap{
      {"assetId", "conflict"}, {"fileName", "IMG_4811.CR2"},
      {"detail", "Older incompatible edits are preserved separately. Keep the saved version to archive this copy locally."},
      {"conflict", true}, {"canArchive", true}}});
  engine.globalObject().setProperty("recoveryChip", engine.newQObject(chip));
  chip->setProperty("archiveRecoveryHandler", QVariant::fromValue(engine.evaluate(R"(
      (function(assetId) {
          if (assetId !== "conflict") return {ok: false, error: "Wrong photo"};
          recoveryChip.recoveryEntries = [];
          recoveryChip.recoveryCount = 0;
          return {ok: true, archiveUrl: "file:///tmp"};
      })
  )")));
  settle();
  if (chip->property("text") != "1 older edit copy" ||
      !QMetaObject::invokeMethod(error_chip, "actionTriggered")) return fail(__LINE__);
  settle();
  auto* review = chip->findChild<QObject*>("recipeRecoveryReviewDialog");
  if (!review || !review->property("visible").toBool()) return fail(__LINE__);
  const auto review_output = qEnvironmentVariable("PHOTO_EDITOR_TEST_RECOVERY_REVIEW_IMAGE");
  if (!review_output.isEmpty() && !window.grabWindow().save(review_output)) return fail(__LINE__);
  const std::function<QQuickItem*(QQuickItem*)> find_archive_button = [&](QQuickItem* parent) -> QQuickItem* {
    if (!parent) return nullptr;
    if (parent->objectName() == "archiveRecipeRecoveryButton") return parent;
    for (auto* child : parent->childItems())
      if (auto* found = find_archive_button(child)) return found;
    return nullptr;
  };
  auto* archive_button = find_archive_button(review->property("contentItem").value<QQuickItem*>());
  if (!archive_button || !QMetaObject::invokeMethod(archive_button, "clicked")) {
    std::cerr << "Could not invoke archive action; button=" << archive_button << '\n';
    return fail(__LINE__);
  }
  settle();
  if (chip->property("recoveryCount").toInt() != 0 ||
      review->property("archiveUrl").toString() != "file:///tmp" ||
      !review->property("visible").toBool()) {
    std::cerr << "Archive receipt: count=" << chip->property("recoveryCount").toInt()
              << " url=" << review->property("archiveUrl").toString().toStdString()
              << " visible=" << review->property("visible").toBool()
              << " error=" << review->property("errorText").toString().toStdString() << '\n';
    return fail(__LINE__);
  }
  QMetaObject::invokeMethod(review, "close");
  settle();
  chip->setParentItem(nullptr);

  // Exercise the production viewport overlay with adjacent inspector controls.
  // Busy state changes must not alter either the controls or the viewport.
  QQmlComponent fixture_component(&engine);
  QByteArray fixture_source = R"(
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
Item {
    id: fixture
    width: 650; height: 360
    property bool working: false
    property bool savingOnly: false
    property bool failed: false
    property int retries: 0
    property int viewportPresses: 0
    property bool spacePanActive: false
    property bool colorMixerImageDragActive: false
    property bool maskLuminanceRangeSampling: false
    property bool masked: false
    property string maskGestureKind: ""
    property bool boardStageActive: false
    property bool canvasBrushActive: false
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property real pointerX: 0
    property real pointerY: 0
    property real loupeX: 0
    property real loupeY: 0
    property bool pixelInspectorSamplingActive: false
    property int recoveryCount: 0
    property bool recoveryBannerVisible: false
    property bool sessionRecoveryAvailable: true
    property int openedWorkspaces: 0
    __RECOVERY_REVIEW_FUNCTION__
    function pixelExactToolReady() { return false }
    function handleSamplingPointerPressed(mouse) { return false }
    function handleSamplingPointerMoved(mouse) { return false }
    function handleSamplingPointerReleased(mouse) { return false }
    function handleMaskPointerPressed(mouse) { return false }
    function handleMaskPointerMoved(mouse, pressed) { return false }
    function handleMaskPointerReleased() {}
    function beginPixelInspectorSampling() { viewportPresses++ }
    function finishPixelInspectorSampling() {}
    function updatePixelInspectorAt(x, y) {}
    Rectangle { width: 420; height: parent.height; color: "#252b30" }
    StudioViewportStatusStack {
        id: status; objectName: "status"
        width: 420; height: parent.height
        viewportObjectName: "edit"
        pixelInspectionRequested: fixture.working
        pixelGridFullResolutionBacked: false
        previewProcessing: fixture.working
        previewContentVisible: true
        editedPreviewSurface: true
        previewRenderFailed: fixture.failed
        previewUpdateBadgeText: fixture.failed ? "Preview failed" : "Rendering preview"
        previewFailureDetail: fixture.failed ? "Test render failure" : ""
        maskActivityProgressValue: -1
        maskActivityChipVisible: fixture.working
        maskActivityChipText: "Selecting subject"
        savePending: fixture.working || fixture.savingOnly
        saveError: ""
        saveRecoveryCount: fixture.recoveryCount
        saveRecoveryDetail: "IMG_4811.CR2: Older edits were preserved separately."
        saveRecoveryEntries: [{assetId: "photo", fileName: "IMG_4811.CR2", detail: "Older edits were preserved separately.", conflict: true, canArchive: false}]
        onRetryRequested: fixture.retries++
    }
    // This is the production pan/zoom surface, placed after status feedback
    // just as it is in StudioImageViewport. It must not swallow status actions.
    StudioViewportInputCommandSurface {
        width: 420; height: parent.height
        viewport: fixture
        workspace: fixture
        previewController: ({})
    }
    ColumnLayout {
        x: 440; width: 190
        Label { text: "Adjust"; color: "white" }
        Label { text: "Exposure"; color: "white" }
        Slider { objectName: "exposure"; Layout.fillWidth: true }
        Label { text: "Contrast"; color: "white" }
        Slider { objectName: "contrast"; Layout.fillWidth: true }
    }
    ActivityStatusBar {
        id: recoveredBar
        objectName: "recoveredBar"
        width: parent.width
        visible: fixture.recoveryBannerVisible
        z: 200
        title: "Work recovered"
        detail: "Recovered your Edit session"
        progressValue: 1
        actionText: "Review work"
        onActionRequested: fixture.reviewRecoveredWork()
    }
    RecoveryReviewDialog {
        id: recoveredWorkReviewDialog
        objectName: "recoveredWorkReviewDialog"
        title: "Recovered work"
        summary: "Recovered your Edit session"
        details: ["Selected photo restored", "Latest committed adjustments restored"]
        recoveryEntries: [{assetId: "photo", fileName: "IMG_4811.CR2", detail: "Older edits were preserved separately.", conflict: true, canArchive: false}]
        workspaceActionText: "Open recovered workspace"
        onOpenWorkRequested: fixture.openedWorkspaces++
    }
})";
  QFile main_qml(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/Main.qml"));
  if (!main_qml.open(QIODevice::ReadOnly)) return fail(__LINE__);
  const auto main_source = main_qml.readAll();
  const auto review_start = main_source.indexOf("    function reviewRecoveredWork() {");
  const auto review_end = main_source.indexOf("\n    function ", review_start + 1);
  if (review_start < 0 || review_end < 0) return fail(__LINE__);
  // Execute Main's actual dispatcher, including the already-in-Edit case.
  fixture_source.replace("__RECOVERY_REVIEW_FUNCTION__", main_source.mid(review_start, review_end - review_start));
  fixture_component.setData(fixture_source, QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/edit-loading-fixture.qml")));
  std::unique_ptr<QObject> fixture(fixture_component.create());
  auto* item = qobject_cast<QQuickItem*>(fixture.get());
  if (!item) { std::cerr << fixture_component.errorString().toStdString(); return fail(__LINE__); }
  window.resize(650, 360);
  item->setParentItem(window.contentItem());
  settle();
  auto* exposure = item->findChild<QQuickItem*>("exposure");
  auto* contrast = item->findChild<QQuickItem*>("contrast");
  auto* status = item->findChild<QQuickItem*>("status");
  auto* render_spinner = item->findChild<QQuickItem*>("editLoadingSpinner");
  if (!exposure || !contrast || !status || !render_spinner) return fail(__LINE__);
  const auto exposure_position = exposure->mapToScene(QPointF{});
  const auto contrast_position = contrast->mapToScene(QPointF{});
  const auto status_size = status->size();
  fixture->setProperty("working", true);
  for (int i = 0; i < 8; ++i) settle();
  if (!render_spinner->isVisible() || exposure->mapToScene(QPointF{}) != exposure_position ||
      contrast->mapToScene(QPointF{}) != contrast_position || status->size() != status_size) return fail(__LINE__);
  if (render_spinner->x() != status->width() - render_spinner->width() - 14 ||
      render_spinner->y() != status->height() - render_spinner->height() - 14) return fail(__LINE__);
  for (auto* child : status->findChildren<QQuickItem*>()) {
    if (child->objectName() == "recipeSaveSpinner" && child->isVisible()) return fail(__LINE__);
  }
  for (auto* child : status->findChildren<QQuickItem*>()) {
    if (child->isVisible() && child->inherits("QQuickText") && !child->property("text").toString().isEmpty())
      return fail(__LINE__);
  }
  if (!output.isEmpty() && !window.grabWindow().save(output + ".busy.png")) return fail(__LINE__);
  fixture->setProperty("failed", true);
  settle();
  auto* failure = item->findChild<QQuickItem*>("editPreviewRenderStatusChip");
  if (!failure || !failure->isVisible() || render_spinner->isVisible()) return fail(__LINE__);
  bool retried = false;
  for (auto* child : failure->findChildren<QObject*>()) {
    if (child->property("text").toString() == "Retry" && child->metaObject()->indexOfSignal("clicked()") >= 0) {
      retried = QMetaObject::invokeMethod(child, "clicked");
      break;
    }
  }
  if (!retried || fixture->property("retries").toInt() != 1) return fail(__LINE__);
  fixture->setProperty("working", false);
  fixture->setProperty("failed", false);
  settle();
  if (render_spinner->isVisible() || exposure->mapToScene(QPointF{}) != exposure_position ||
      contrast->mapToScene(QPointF{}) != contrast_position) return fail(__LINE__);
  fixture->setProperty("savingOnly", true);
  settle();
  if (!render_spinner->isVisible() || exposure->mapToScene(QPointF{}) != exposure_position ||
      contrast->mapToScene(QPointF{}) != contrast_position) return fail(__LINE__);
  fixture->setProperty("savingOnly", false);
  settle();
  if (render_spinner->isVisible()) return fail(__LINE__);
  fixture->setProperty("recoveryCount", 1);
  settle();
  auto* overlay_chip = find_item(status, "recipeSaveError");
  QQuickItem* review_button = nullptr;
  if (overlay_chip) {
    for (auto* child : overlay_chip->findChildren<QQuickItem*>())
      if (child->property("text").toString() == "Review" && child->isVisible()) {
        review_button = child;
        break;
      }
  }
  const auto presses_before = fixture->property("viewportPresses").toInt();
  if (!click_item(review_button)) { std::cerr << "No visible pointer target for recovery review.\n"; return fail(__LINE__); }
  settle();
  auto* overlay_review = status->findChild<QObject*>("recipeRecoveryReviewDialog");
  if (!overlay_review || !overlay_review->property("visible").toBool() ||
      fixture->property("viewportPresses").toInt() != presses_before) {
    std::cerr << "The viewport swallowed the physical recovery Review click.\n";
    return fail(__LINE__);
  }
  QMetaObject::invokeMethod(overlay_review, "close");
  settle();
  fixture->setProperty("recoveryCount", 0);
  settle();
  QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, QPoint(160, 160));
  settle();
  if (fixture->property("viewportPresses").toInt() != presses_before + 1) {
    std::cerr << "Status feedback must let ordinary canvas clicks through.\n";
    return fail(__LINE__);
  }
  fixture->setProperty("recoveryBannerVisible", true);
  settle();
  auto* recovered_bar = find_item(item, "recoveredBar");
  if (!click_item(find_item(recovered_bar, "activityCancelButton"))) return fail(__LINE__);
  settle();
  auto* work_review = item->findChild<QObject*>("recoveredWorkReviewDialog");
  if (!work_review || !work_review->property("visible").toBool() ||
      work_review->property("details").toList().size() != 2 ||
      work_review->property("recoveryEntries").toList().size() != 1 ||
      fixture->property("openedWorkspaces").toInt() != 0 ||
      !fixture->property("sessionRecoveryAvailable").toBool()) {
    std::cerr << "Review work must open the summary before navigation or dismissal.\n";
    return fail(__LINE__);
  }
  const auto work_review_output = qEnvironmentVariable("PHOTO_EDITOR_TEST_WORK_RECOVERY_IMAGE");
  if (!work_review_output.isEmpty() && !window.grabWindow().save(work_review_output)) return fail(__LINE__);
  auto* work_content = work_review->property("contentItem").value<QQuickItem*>();
  if (!click_item(find_item(work_content, "openRecoveredWorkspaceButton"))) return fail(__LINE__);
  for (int i = 0; i < 20 && work_review->property("visible").toBool(); ++i) settle();
  if (fixture->property("openedWorkspaces").toInt() != 1 || work_review->property("visible").toBool()) {
    std::cerr << "Open recovered workspace: calls=" << fixture->property("openedWorkspaces").toInt()
              << " visible=" << work_review->property("visible").toBool() << '\n';
    return fail(__LINE__);
  }
  QFile workspace(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/StudioWorkspace.qml"));
  if (!workspace.open(QIODevice::ReadOnly) || workspace.readAll().contains("RecipeSaveStatusChip {")) return fail(__LINE__);
  item->setParentItem(nullptr);
  std::cout << "Recovery mouse clicks beat canvas input; Review work opens the real summary dispatcher; archive, canvas pass-through and activity feedback passed.\n";
  return 0;
}
