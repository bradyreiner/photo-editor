#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QVariant>
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
import "../apps/desktop/qml/components"
QtObject {
    id: test
    property int readyCount: 0
    property bool opened: false
    property string readyId: ""
    property QtObject view: QtObject {
        property var status: ({})
        property int calls: 0
        function exportPhoto(path, edge) { calls++; status = {exporting: true}; return {ok: true, outputPath: path} }
    }
    property QtObject library: QtObject {
        property bool busy: false
        property bool reject: false
        property int calls: 0
        property string errorMessage: ""
        property string found: ""
        property bool recipeReady: true
        property string preparedId: ""
        property var importArguments: []
        function parentFolderUrl(path) { return "file:///photos" }
        function localPathForUrl(path) { return "/photos" }
        function assetIdForFilePath(path) { return found }
        function prepareRenderedPhotoForEditing(assetId) { preparedId = assetId; return recipeReady }
        function importSelectionWithOptions(folder, paths, project, mode) {
            calls++; importArguments = [folder, paths[0], project, mode]; busy = !reject
            errorMessage = reject ? "database unavailable" : ""
        }
    }
    property ScenePhotoEditHandoff flow: ScenePhotoEditHandoff {
        viewport: test.view
        catalog: test.library
        sceneIdentity: "scene-a"
        onPhotoReady: (assetId, openEditor) => { test.readyCount++; test.opened = openEditor; test.readyId = assetId }
    }
    function rendered(path) { view.status = {exporting: false, lastExport: path}; flow.checkProgress() }
    function exercise(): var {
        if (!flow.start("/photos/one.png", 800).ok || flow.phase !== "rendering" || library.calls !== 0)
            return "render must finish before import"
        if (flow.start("/photos/duplicate.png", 800).ok || flow.pendingPath !== "/photos/one.png" || view.calls !== 1)
            return "duplicate admission must preserve the pending job"
        flow.checkProgress()
        if (library.calls !== 0) return "active rendering must not import"
        rendered("/photos/one.png")
        if (flow.phase !== "importing" || library.calls !== 1 || readyCount !== 0 ||
            JSON.stringify(library.importArguments) !== JSON.stringify(["/photos", "/photos/one.png", "Scene renders", "referenced"]))
            return "completed photo must use normal referenced import"
        library.found = "asset-one"
        flow.checkProgress()
        if (readyCount !== 0) return "busy catalog must settle before opening editor"
        library.busy = false; flow.checkProgress(); flow.checkProgress()
        if (readyCount !== 1 || !opened || readyId !== "asset-one" || library.preparedId !== "asset-one" || flow.busy)
            return "successful import must open exactly the catalog asset once"
        library.found = ""
        flow.start("/photos/two.png", 800); flow.sceneIdentity = "scene-b"; rendered("/photos/two.png")
        library.found = "asset-two"; library.busy = false; flow.checkProgress()
        if (readyCount !== 2 || opened) return "changing scenes must suppress automatic navigation"
        library.found = ""; flow.start("/photos/recipe-error.png", 800); rendered("/photos/recipe-error.png")
        library.found = "asset-bad-recipe"; library.busy = false; library.recipeReady = false; flow.checkProgress()
        if (readyCount !== 2 || flow.busy || flow.errorMessage.indexOf("recipe") < 0)
            return "recipe preparation failure must retain the import without opening it"
        library.recipeReady = true
        library.found = ""; library.busy = true
        const before = view.calls
        if (flow.start("/photos/busy.png", 800).ok || view.calls !== before) return "busy catalog must reject early"
        library.busy = false; flow.start("/photos/late-busy.png", 800); library.busy = true
        const imports = library.calls
        rendered("/photos/late-busy.png")
        if (flow.busy || library.calls !== imports || flow.errorMessage.length === 0) return "late busy catalog must not start an untracked import"
        library.busy = false; flow.start("/photos/error.png", 800)
        view.status = {exporting: false, error: "disk full", lastExport: "/photos/two.png"}; flow.checkProgress()
        if (flow.busy || flow.errorMessage !== "disk full" || library.calls !== imports) return "failed publication must never import a stale export"
        flow.start("/photos/import-error.png", 800); library.reject = true; rendered("/photos/import-error.png")
        if (flow.busy || flow.errorMessage.indexOf("database unavailable") < 0 || readyCount !== 2)
            return "import failure must remain visible without opening editor"
        library.reject = false; flow.start("/photos/timeout.png", 800); flow.startedAt = Date.now() - 180001; flow.checkProgress()
        if (flow.busy || flow.errorMessage.indexOf("timed out") < 0) return "handoff timeout must be bounded and visible"
        return ""
    }
}
)QML", QUrl::fromLocalFile(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/tests/scene-photo-edit-fixture.qml")));
  std::unique_ptr<QObject> object(component.create());
  if (!object) { std::cerr << component.errorString().toStdString(); return 1; }
  QVariant result;
  if (!QMetaObject::invokeMethod(object.get(), "exercise", Q_RETURN_ARG(QVariant, result)) || !result.toString().isEmpty()) {
    std::cerr << "Scene photo handoff: " << result.toString().toStdString() << '\n'; return 1;
  }
  std::cout << "render_import_open=1 frozen_scene_navigation=1 errors_and_timeout=1 single_flight=1\n";
}
