#include <QGuiApplication>
#include <QElapsedTimer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QThread>
#include <iostream>
#include <memory>

namespace {
bool wait_for(auto predicate, int timeout = 3000) {
  QElapsedTimer timer; timer.start();
  while (!predicate() && timer.elapsed() < timeout) {
    QCoreApplication::processEvents(); QThread::msleep(2);
  }
  return predicate();
}
bool call(QObject* object, const char* method, const QString& text) {
  return QMetaObject::invokeMethod(object, method, Q_ARG(QVariant, text));
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import QtQuick.Controls.Basic
import "file:)" PHOTO_EDITOR_SOURCE_DIR R"(/apps/desktop/qml/components"
ApplicationWindow {
    width: 800; height: 700; visible: true
    QtObject {
        id: fixtureCatalog; objectName: "catalog"
        property string searchFilter: ""
        property string projectFilter: "Portraits"
        property int minimumRatingFilter: 3
        property bool loading: false
        property bool visualSearchActive: false
        property int assetCount: visualSearchActive ? 2 : searchFilter === "" ? 3 : searchFilter === "IMG_0102" ? 1 : 0
        signal filtersChanged()
        onSearchFilterChanged: filtersChanged()
    }
    QtObject {
        id: encoder; objectName: "encoder"
        property bool configured: true
        property bool running: false
        property string status: "Ready"
    }
    QtObject {
        id: binding; objectName: "binding"
        property var controller: encoder
        property int searches: 0
        property int cancellations: 0
        property string query: ""
        property bool scopeWasUnfiltered: false
        function search(value) {
            searches++; query = value
            scopeWasUnfiltered = fixtureCatalog.searchFilter === "" && fixtureCatalog.projectFilter === "Portraits" && fixtureCatalog.minimumRatingFilter === 3
            encoder.running = true; encoder.status = "Indexing current previews"
        }
        function cancel() { cancellations++; encoder.running = false; fixtureCatalog.visualSearchActive = false }
        function finish() { encoder.running = false; encoder.status = "2 similar photos"; fixtureCatalog.visualSearchActive = true }
    }
    QtObject {
        id: setup; objectName: "setup"
        property bool ready: true
        property bool running: false
        property bool available: true
        property string status: "Search model ready"
        property string location: "/fixture/search-model"
        property real progress: 1
        property int checks: 0
        function verify() { checks++ }
        function download(confirmed) {}
        function cancel() {}
        signal changed()
    }
    QtObject {
        id: fixtureAgent; objectName: "agent"
        property var availableModels: [{id: "one", name: "Vision One", available: true},
                                      {id: "two", name: "Vision Two", available: true}]
        property string selectedModelId: "one"
        property bool running: false
        property bool modelInstallRunning: false
        property bool modelProbeRunning: false
        property int probes: 0
        signal modelInstallChanged()
        function prepareSelectedModelInstall() { return {} }
        function probeSelectedModelAsync(force) { probes++ }
    }
    LibrarySearchCoordinator { id: search; objectName: "search"; catalog: fixtureCatalog; similarity: binding; setup: setup }
    AiModelsSettings {
        objectName: "models"; width: 760
        searchSetup: setup; agent: fixtureAgent; preview: null
    }
})", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/library-search-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
  auto* search = object->findChild<QObject*>("search");
  auto* catalog = object->findChild<QObject*>("catalog");
  auto* binding = object->findChild<QObject*>("binding");
  auto* encoder = object->findChild<QObject*>("encoder");
  auto* setup = object->findChild<QObject*>("setup");
  if (!search || !catalog || !binding || !encoder || !setup) return fail("Fixture missing");
  auto* picker = object->findChild<QObject*>("settingsAgentModelPicker");
  auto* agent = object->findChild<QObject*>("agent");
  auto* check = object->findChild<QObject*>("settingsSearchModelVerify");
  if (!picker || !agent || !check) return fail("Settings model controls missing");
  picker->setProperty("currentIndex", 1);
  if (!QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, 1)) ||
      agent->property("selectedModelId").toString() != "two" || agent->property("probes").toInt() != 1 ||
      !QMetaObject::invokeMethod(check, "clicked") || setup->property("checks").toInt() != 1)
    return fail("Settings model selection/check actions did not reach their controllers");
  if (!call(search, "edit", "IMG_0102") || !QMetaObject::invokeMethod(search, "apply") ||
      catalog->property("assetCount").toInt() != 1 || binding->property("searches").toInt() != 0)
    return fail("Filename search should remain immediate without model work");
  if (!call(search, "edit", "girl") || !wait_for([&] { return binding->property("searches").toInt() == 1; }) ||
      !binding->property("scopeWasUnfiltered").toBool() || search->property("text").toString() != "girl" ||
      !search->property("searching").toBool()) return fail("Main-bar description did not search photo content in the retained scope");
  QMetaObject::invokeMethod(binding, "finish");
  if (!catalog->property("visualSearchActive").toBool() || search->property("text").toString() != "girl")
    return fail("Projection erased the user's query");
  if (!call(search, "edit", "green hat") || catalog->property("visualSearchActive").toBool() ||
      !wait_for([&] { return binding->property("searches").toInt() == 2; }) ||
      binding->property("query").toString() != "green hat") return fail("Replacement query retained stale results");
  QMetaObject::invokeMethod(search, "clear");
  if (encoder->property("running").toBool() || !search->property("text").toString().isEmpty() ||
      !catalog->property("searchFilter").toString().isEmpty() || catalog->property("projectFilter").toString() != "Portraits")
    return fail("Clear did not cancel content search and preserve other filters");
  encoder->setProperty("configured", false);
  if (!call(search, "edit", "girl") || !wait_for([&] { return search->property("status").toString().contains("Settings"); }) ||
      binding->property("searches").toInt() != 2) return fail("Missing model should direct setup to Settings");
  encoder->setProperty("configured", true);
  QMetaObject::invokeMethod(setup, "changed");
  if (!wait_for([&] { return binding->property("searches").toInt() == 3; }))
    return fail("Installing the model should resume the pending description");
  search->setProperty("active", false);
  if (encoder->property("running").toBool()) return fail("Leaving the Library should cancel search");
  std::cout << "Main-bar filename/content routing, query replacement, clear, scope, model readiness and settings component passed\n";
  return 0;
}
