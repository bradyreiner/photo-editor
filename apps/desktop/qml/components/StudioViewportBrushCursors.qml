pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

Item {
    id: cursorStack

    required property string viewportObjectName
    required property var pixelBrushCell
    required property bool pixelBrushVisible
    required property color pixelBrushColor
    required property real paintedContentWidth
    required property real paintedContentHeight
    required property real paintedContentX
    required property real paintedContentY
    required property real pointerX
    required property real pointerY
    required property real activeBrushSize
    required property real activeBrushHardness
    required property string activeMaskOperation
    required property bool brushVisible
    required property color brushOperationColor
    required property string brushSizeText
    required property string brushHardnessText
    required property string brushFlowText
    required property string brushDensityText

    function objectNameFor(suffix, fallbackName) {
        return cursorStack.viewportObjectName.length > 0
                ? cursorStack.viewportObjectName + suffix
                : fallbackName
    }

    function clamp(value, minimum, maximum) {
        return Math.max(minimum, Math.min(maximum, value))
    }

    Rectangle {
        id: pixelBrushSnappedCursor
        objectName: cursorStack.objectNameFor("PixelBrushSnappedCursor", "pixelBrushSnappedCursor")
        readonly property var cell: cursorStack.pixelBrushCell
        readonly property string pixelBrushCursorSnapContract: "pixel-brush-cursor:v2|source-cell:floor|screen-rect:mapper|visible:pixel-grid|source-backed:pixelGridFullResolutionBacked"
        visible: cursorStack.pixelBrushVisible && pixelBrushSnappedCursor.cell.valid
        x: pixelBrushSnappedCursor.cell.x
        y: pixelBrushSnappedCursor.cell.y
        width: pixelBrushSnappedCursor.cell.width
        height: pixelBrushSnappedCursor.cell.height
        radius: 0
        color: theme.alpha(
                   cursorStack.pixelBrushColor,
                   0.24)
        border.width: 1
        border.color: theme.alpha(
                          cursorStack.pixelBrushColor,
                          0.95)
        z: 21
    }

    Rectangle {
        id: brushCursorRing
        objectName: cursorStack.objectNameFor("BrushCursorRing", "brushCursorRing")
        readonly property real brushDiameter: Math.max(
            8,
            Math.min(cursorStack.paintedContentWidth, cursorStack.paintedContentHeight) *
                cursorStack.activeBrushSize * 2)
        readonly property real hardnessDiameter: Math.max(
            2,
            brushCursorRing.brushDiameter * cursorStack.activeBrushHardness)
        readonly property string brushCursorVisibilityContract: "persistent-last-position|dual-contrast-ring|hardness-inner-ring:v1"
        readonly property string brushModeLabel: cursorStack.activeMaskOperation === "subtract"
                                                ? "Erase"
                                                : "Add"
        visible: cursorStack.brushVisible
        x: cursorStack.clamp(cursorStack.pointerX - width / 2,
                             cursorStack.paintedContentX - width / 2,
                             cursorStack.paintedContentX +
                                 cursorStack.paintedContentWidth - width / 2)
        y: cursorStack.clamp(cursorStack.pointerY - height / 2,
                             cursorStack.paintedContentY - height / 2,
                             cursorStack.paintedContentY +
                                 cursorStack.paintedContentHeight - height / 2)
        width: brushCursorRing.brushDiameter
        height: brushCursorRing.brushDiameter
        radius: width / 2
        color: "transparent"
        z: 22

        Rectangle {
            objectName: cursorStack.objectNameFor("BrushCursorOuterHaloRing", "brushCursorOuterHaloRing")
            anchors.fill: parent
            radius: width / 2
            color: "transparent"
            border.width: 3.0
            border.color: theme.alpha(theme.ink950, 0.92)
        }

        Rectangle {
            objectName: cursorStack.objectNameFor("BrushCursorFeatherRing", "brushCursorFeatherRing")
            anchors.fill: parent
            anchors.margins: 1
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: cursorStack.brushOperationColor
        }

        Rectangle {
            objectName: cursorStack.objectNameFor("BrushCursorHardnessRing", "brushCursorHardnessRing")
            anchors.centerIn: parent
            width: brushCursorRing.hardnessDiameter
            height: width
            radius: width / 2
            color: "transparent"
            border.width: 1.5
            border.color: theme.alpha(theme.polar, 0.92)
        }

        Item {
            objectName: cursorStack.objectNameFor("BrushCursorCenter", "brushCursorCenter")
            anchors.centerIn: parent
            width: 10
            height: 10
            visible: brushCursorRing.width >= 24
            Rectangle {
                anchors.centerIn: parent
                width: 9; height: 3
                color: theme.ink950
            }
            Rectangle {
                anchors.centerIn: parent
                width: 7; height: 1
                color: theme.brandPaper
            }
            Rectangle {
                anchors.centerIn: parent
                width: 3; height: 9
                visible: cursorStack.activeMaskOperation !== "subtract"
                color: theme.ink950
            }
            Rectangle {
                anchors.centerIn: parent
                width: 1; height: 7
                visible: cursorStack.activeMaskOperation !== "subtract"
                color: theme.brandPaper
            }
        }

    }

    Label {
        id: brushCursorReadout
        objectName: cursorStack.objectNameFor("BrushCursorModeLabel", "brushCursorModeLabel")
        visible: brushCursorRing.visible
        readonly property real viewportMargin: theme.space8
        width: Math.max(1, Math.min(implicitWidth, cursorStack.width - viewportMargin * 2))
        x: cursorStack.clamp(
               brushCursorRing.x + (brushCursorRing.width - width) / 2,
               viewportMargin,
               Math.max(viewportMargin, cursorStack.width - width - viewportMargin))
        y: cursorStack.clamp(
               brushCursorRing.y + brushCursorRing.height + theme.space4,
               viewportMargin,
               Math.max(viewportMargin, cursorStack.height - height - viewportMargin))
        text: brushCursorRing.brushModeLabel + " Size " +
              cursorStack.brushSizeText +
              " / Hard " + cursorStack.brushHardnessText +
              " / Flow " + cursorStack.brushFlowText +
              " / Density " + cursorStack.brushDensityText
        color: theme.textStrong
        font.family: theme.fontMono
        font.pixelSize: theme.typeCaption
        padding: 4
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
        z: 23
        background: Rectangle {
            color: theme.alpha(theme.ink950, 0.78)
            border.width: theme.chromeBorderWidth
            border.color: theme.alpha(theme.polar, 0.28)
            radius: 2
        }
    }

    Theme { id: theme }
}
