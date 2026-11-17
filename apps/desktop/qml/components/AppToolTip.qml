pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ToolTip {
    id: root

    // Tooltips are visual-only. Keep the popup out of pointer and keyboard
    // routing so its fade-out cannot swallow the first click on a control below.
    enabled: false
    modal: false
    focus: false
    readonly property QtObject pointerPassthroughMask: QtObject {
        function contains(point: point): bool { return false }
    }

    property int maximumTextWidth: theme.scaled(300)
    property Item targetItem
    property bool followPointer: false
    property point pointerPosition: Qt.point(0, 0)
    property string shortcutText: ""

    readonly property real preferredTextWidth: Math.min(
                                                   maximumTextWidth,
                                                   parent ? Math.max(1, parent.width - theme.space8 * 2 - leftPadding - rightPadding) : maximumTextWidth,
                                                   Math.max(40, tooltipLabel.implicitWidth, shortcutLabel.implicitWidth))
    property point anchorOrigin: Qt.point(0, 0)

    function updateAnchor() {
        if (targetItem && parent)
            anchorOrigin = targetItem.mapToItem(parent, 0, 0)
    }

    padding: theme.space10
    delay: 300
    timeout: 3000
    leftPadding: theme.space8
    rightPadding: theme.space8
    topPadding: theme.space6
    bottomPadding: theme.space6
    implicitWidth: preferredTextWidth + leftPadding + rightPadding
    implicitHeight: tooltipContent.implicitHeight + topPadding + bottomPadding
    parent: Overlay.overlay
    x: parent ? Math.max(theme.space8, Math.min(parent.width - width - theme.space8,
                                     followPointer ? pointerPosition.x + 14
                                                   : anchorOrigin.x + (targetItem ? targetItem.width - width : 0) / 2)) : 0
    y: parent ? (followPointer
                 ? (pointerPosition.y + height + 16 < parent.height
                    ? pointerPosition.y + 14
                    : Math.max(theme.space8, pointerPosition.y - height - theme.space12))
                 : (anchorOrigin.y > height + 12
                    ? anchorOrigin.y - height - theme.space8
                    : anchorOrigin.y + (targetItem ? targetItem.height : 0) + theme.space8)) : 0
    z: 1000
    onAboutToShow: {
        root.updateAnchor()
        if (String(root.text).trim().length === 0)
            root.close()
    }
    onOpened: root.updateAnchor()

    enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: theme.opticalHoverDuration } }
    exit: Transition { NumberAnimation { property: "opacity"; to: 0; duration: theme.reducedMotion ? 0 : 70 } }

    contentItem: Column {
        id: tooltipContent
        width: root.preferredTextWidth
        spacing: theme.space4
        Text {
            id: tooltipLabel
            text: root.text
            width: root.preferredTextWidth
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            lineHeight: 1.2
            wrapMode: Text.Wrap
            renderType: Text.NativeRendering
        }
        Text {
            id: shortcutLabel
            visible: root.shortcutText.length > 0
            text: root.shortcutText
            width: root.preferredTextWidth
            color: theme.opticalLight
            font.family: theme.fontNumeric
            font.features: theme.numericFontFeatures
            font.pixelSize: theme.typeMicro
            wrapMode: Text.Wrap
            renderType: Text.NativeRendering
        }
    }

    background: PopupSurface { shadowColor: "#40000000" }

    Binding {
        target: root.contentItem ? root.contentItem.parent : null
        property: "containmentMask"
        value: root.pointerPassthroughMask
        when: root.contentItem !== null && root.contentItem.parent !== null
    }

    Theme { id: theme }
}
