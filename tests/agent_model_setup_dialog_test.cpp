#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QThread>
#include <iostream>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
  width: 660; height: 560; visible: true; color: "#161b20"
  QtObject {
    id: fixtureController
    objectName: "fixtureController"
    property var availableModels: [{id: "managed-vision", name: "Qwen3-VL 32B", model: "qwen3-vl:32b",
      runtime: "managed", runtimeState: "ready", available: false, statusReason: "Download the selected vision model to continue."}]
    property string selectedModelId: "managed-vision"
    property bool modelInstallRunning: false
    property string modelInstallStatus: "Install failed"
    property string modelInstallOutput: "The registry manifest does not match the approved model pin. No model blobs were downloaded."
    property var modelInstallProgress: ({status: "failed", ollamaModel: "qwen3-vl:32b"})
    property double cachedBytes: 20000000000
    signal modelInstallChanged()
    function prepareSelectedModelInstall() { return {modelId: selectedModelId, runtime: "managed", installable: true,
      artifactBytes: 20910287340, estimatedCachedBytes: cachedBytes, requiredFreeBytes: 21984029164 - cachedBytes, license: "Apache-2.0"} }
    function installSelectedModel(confirmed) {
      if (!confirmed) return {errors: ["Confirmation required"]}
      modelInstallRunning = true
      modelInstallStatus = "Checking pinned model manifest"
      modelInstallProgress = {status: "starting", ollamaModel: "qwen3-vl:32b"}
      modelInstallChanged()
      return {ok: true}
    }
    function cancelModelInstall() {}
    function retryLocalRuntime() {}
  }
  AgentModelSetupDialog { id: setup; parent: Overlay.overlay; controller: fixtureController }
  Component.onCompleted: setup.present()
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/model-setup-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  auto* window = qobject_cast<QQuickWindow*>(object.get());
  if (!window) { std::cerr << component.errorString().toStdString(); return EXIT_FAILURE; }
  auto* dialog = window->findChild<QObject*>("agentModelSetupDialog");
  auto* detail = window->findChild<QObject*>("modelSetupStatusDetail");
  auto* controller = window->findChild<QObject*>("fixtureController");
  auto* storage = window->findChild<QObject*>("modelSetupStorageDetail");
  QElapsedTimer settle;
  settle.start();
  while (dialog && !dialog->property("opened").toBool() && settle.elapsed() < 2000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(5);
  }
  if (!dialog || !detail || !controller || !storage || !dialog->property("opened").toBool() ||
      detail->property("text") != controller->property("modelInstallOutput")) return EXIT_FAILURE;
  if (!storage->property("text").toString().contains("20.0 GB cached; checked before reuse") ||
      !storage->property("text").toString().contains("1.98 GB free")) return EXIT_FAILURE;
  const auto output = qEnvironmentVariable("PHOTO_EDITOR_TEST_MODEL_SETUP_IMAGE");
  if (!output.isEmpty() && !window->grabWindow().save(output)) return EXIT_FAILURE;
  controller->setProperty("cachedBytes", 0.0);
  QMetaObject::invokeMethod(controller, "modelInstallChanged");
  QCoreApplication::processEvents();
  if (storage->property("text").toString().contains("GB cached") ||
      !storage->property("text").toString().contains("21.98 GB free")) return EXIT_FAILURE;
  auto* retry = window->findChild<QObject*>("confirmModelDownloadButton");
  if (!retry || !retry->property("enabled").toBool() || !QMetaObject::invokeMethod(retry, "clicked")) return EXIT_FAILURE;
  QCoreApplication::processEvents();
  if (!controller->property("modelInstallRunning").toBool() || detail->property("text") != "Checking pinned model manifest") return EXIT_FAILURE;
  controller->setProperty("modelInstallRunning", false);
  controller->setProperty("modelInstallProgress", QVariantMap{{"status", "failed"}, {"ollamaModel", "another-model"}});
  QMetaObject::invokeMethod(controller, "modelInstallChanged");
  QCoreApplication::processEvents();
  if (detail->property("text") == controller->property("modelInstallOutput")) return EXIT_FAILURE;
  std::cout << "Setup card: download error visible, confirmed retry clears error, other-model failure excluded.\n";
  return EXIT_SUCCESS;
}
