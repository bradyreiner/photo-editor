import QtQuick
import QtQuick.Window
import "../apps/desktop/qml/components"

Window {
    id: root
    width: 1000
    height: 600
    visible: true
    color: "#171717"
    property real photoX: -1
    property real timelineX: -1
    Theme {
        id: theme
    }
    CanvasGuideState {
        id: state
        objectName: "guideState"
        workspaceKey: "Test"
        documentKey: "photo-a"
        canvasWidth: 1800
        canvasHeight: 1000
    }
    Rectangle {
        x: 60
        y: 60
        width: 900
        height: 500
        color: "#303e48"
    }
    Rectangle {
        x: 200
        y: 190
        width: 160
        height: 100
        color: "#b3c4bc"
    }
    Text {
        x: 75
        y: 565
        text: "Drag from either ruler · Shift snaps to multiples of five · Click px to change units"
        color: "white"
    }
    CanvasGuideOverlay {
        id: overlay
        objectName: "guideOverlay"
        anchors.fill: parent
        guideState: state
        imageX: 60
        imageY: 60
        imageWidth: 900
        imageHeight: 500
    }
    LayerTransformOverlay {
        id: photo
        visible: false
        active: true
        moveSelection: true
        imageX: 60
        imageY: 60
        imageWidth: 900
        imageHeight: 500
        canvasWidth: 1800
        canvasHeight: 1000
        layerX: 100
        layerY: 100
        layerWidth: 100
        layerHeight: 100
        snapTargetsX: state.snapTargets(true)
        snapTargetsY: state.snapTargets(false)
        onPositionEdited: function (x, y) {
            root.photoX = x;
        }
    }
    TimelineTransformOverlay {
        id: timeline
        visible: false
        theme: theme
        width: 900
        height: 500
        frameScale: 0.2
        guideState: state
        onTransformEdited: function (x, y, scale, rotation) {
            root.timelineX = x;
        }
    }
    Item {
        id: titleViewport
        width: 900
        height: 500
        Item {
            id: title
            x: 198
            y: 100
            width: 100
            height: 50
        }
    }

    function check(condition, message) {
        if (!condition)
            throw new Error(message);
    }
    function near(a, b) {
        return Math.abs(a - b) < 0.0001;
    }
    function reset() {
        state.locked = false;
        state.documentKey = "photo-a";
        state.clear();
        state.unit = "px";
        state.pixelsPerInch = 96;
        state.guidesVisible = true;
        state.rulersVisible = true;
        state.snappingEnabled = true;
    }
    function modelChecks() {
        try {
            reset();
            check(near(state.quantize(433, true, true), 435), "pixel five-unit rounding");
            state.unit = "in";
            state.pixelsPerInch = 100;
            check(near(state.quantize(730, true, true), 500), "inch rounding uses PPI");
            state.unit = "cm";
            state.pixelsPerInch = 254;
            check(near(state.pixelsPerUnit(true), 100), "centimeter conversion");
            check(near(state.quantize(740, true, true), 500), "centimeter five-unit rounding");
            state.unit = "%";
            check(near(state.quantize(433, true, true), 450), "percentage uses width");
            check(near(state.quantize(218, false, true), 200), "percentage uses height");
            check(near(state.quantize(433.25, true, false), 433.25), "free placement keeps precision");
            state.unit = "px";
            state.commitGuide(-1, "x", 600, false);
            check(near(state.snapDelta([594], true, 0.5, 6), 6), "zoom-aware snap");
            check(near(state.snapDelta([580], true, 0.5, 6), 0), "outside snap tolerance");
            state.documentKey = "photo-b";
            state.clear();
            check(state.guides.length === 0, "guides are isolated per document");
            state.documentKey = "photo-a";
            check(state.guides.length === 1, "document guides restore");
            state.locked = true;
            state.clear();
            state.commitGuide(-1, "y", 200, false);
            check(state.guides.length === 1, "locked guides cannot change");
            state.locked = false;
            photo.beginExternalMove(Qt.point(0, 0));
            photo.moveFromDrag(Qt.point(197.5, 0), Qt.NoModifier);
            check(near(photoX, 500), "Edit layer right edge snaps to custom guide");
            photo.moveFromDrag(Qt.point(197.5, 0), Qt.AltModifier);
            check(near(photoX, 495), "Alt bypasses Edit snapping");
            photo.endInteraction();
            timeline.begin("move", Qt.point(450, 250), 0, 0);
            timeline.update(Qt.point(245, 250), Qt.NoModifier);
            timeline.finish(false);
            check(near(timelineX, -200 * 4 / 900), "Timeline right edge snaps to guide");
            timeline.begin("move", Qt.point(450, 250), 0, 0);
            timeline.update(Qt.point(245, 250), Qt.AltModifier);
            timeline.finish(false);
            check(near(timelineX, -205 * 4 / 900), "Alt bypasses Timeline snapping");
            check(near(state.snapItemDelta(title, titleViewport, 0, 0, false).x, 2), "Title edge snaps in Program coordinates");
            state.guidesVisible = false;
            check(state.snapTargets(true).length === 0, "hidden guides do not snap");
            state.guidesVisible = true;
            state.snappingEnabled = false;
            check(state.snapTargets(true).length === 0, "snap toggle is respected");
            reset();
            return "ok";
        } catch (error) {
            return String(error);
        }
    }
    function assertGuides(count, axis, position) {
        if (state.guides.length !== count)
            return "Unexpected guide count: " + state.guides.length;
        if (count > 0 && (state.guides[count - 1].axis !== axis || !near(state.guides[count - 1].position, position)))
            return "Unexpected guide: " + JSON.stringify(state.guides);
        return "ok";
    }
    function zoomAndPan() {
        overlay.imageX = 80;
        overlay.imageWidth = 1800;
    }
    function prepareCapture() {
        reset();
        state.commitGuide(-1, "x", 600, false);
        state.commitGuide(-1, "x", 1200, false);
        state.commitGuide(-1, "y", 400, false);
    }
}
