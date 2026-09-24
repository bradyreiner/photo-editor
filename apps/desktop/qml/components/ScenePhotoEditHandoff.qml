pragma ComponentBehavior: Bound
import QtQuick

QtObject {
    id: root
    required property var viewport
    required property var catalog
    property string sceneIdentity: ""
    property string phase: ""
    property string pendingPath: ""
    property string frozenSceneIdentity: ""
    property real startedAt: 0
    property string errorMessage: ""
    property string completedPath: ""
    readonly property bool busy: phase.length > 0
    readonly property string statusText: phase === "rendering" ? "Rendering photo for editing…"
        : phase === "importing" ? "Adding rendered photo to Library…"
        : completedPath.length > 0 ? "Photo added to Library: " + completedPath : ""
    signal photoReady(string assetId, bool openEditor)

    function fail(message: string): var {
        errorMessage = message
        phase = ""
        pendingPath = ""
        return {ok: false, error: message}
    }
    function start(destination: string, edge: int): var {
        if (busy)
            return {ok: false, error: "A photo is already being prepared for editing."}
        if (!catalog || catalog.busy)
            return fail("Wait for the Library operation to finish before editing a rendered photo.")
        errorMessage = ""
        completedPath = ""
        const result = viewport.exportPhoto(destination, edge)
        if (!result.ok)
            return fail(String(result.error || "Photo rendering could not start."))
        pendingPath = String(result.outputPath)
        frozenSceneIdentity = sceneIdentity
        startedAt = Date.now()
        phase = "rendering"
        return result
    }
    function finish(assetId: string): void {
        const sameScene = frozenSceneIdentity === sceneIdentity
        completedPath = pendingPath
        pendingPath = ""
        phase = ""
        photoReady(assetId, sameScene)
    }
    function checkProgress(): void {
        if (!busy)
            return
        if (Date.now() - startedAt > 180000) {
            fail("The edit handoff timed out. Any rendered photo or completed import is retained; check Library before retrying.")
            return
        }
        if (phase === "rendering") {
            if (viewport.status.exporting)
                return
            if (String(viewport.status.lastExport || "") !== pendingPath) {
                fail(String(viewport.status.error || "The rendered photo was not published."))
                return
            }
            if (catalog.busy) {
                fail("Photo saved, but Library is busy. Import the saved photo when the current operation finishes.")
                return
            }
            phase = "importing"
            catalog.importSelectionWithOptions(catalog.localPathForUrl(catalog.parentFolderUrl(pendingPath)),
                                               [pendingPath], "Scene renders", "referenced")
        }
        if (phase === "importing" && !catalog.busy) {
            const assetId = String(catalog.assetIdForFilePath(pendingPath) || "")
            if (assetId.length > 0) {
                if (catalog.prepareRenderedPhotoForEditing(assetId))
                    finish(assetId)
                else
                    fail("Photo imported, but its color-preserving edit recipe could not be prepared. The saved photo is retained in Library.")
            } else if (String(catalog.errorMessage || "").length > 0) {
                fail("Photo saved, but Library import failed: " + catalog.errorMessage)
            }
        }
    }
    property Timer pollTimer: Timer {
        interval: 100
        repeat: true
        running: root.busy
        onTriggered: root.checkProgress()
    }
}
