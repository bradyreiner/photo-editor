import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects

ToolTip {
    id: root

    property int maximumTextWidth: 300
    property Item targetItem

    readonly property real preferredTextWidth: Math.min(
                                                   maximumTextWidth,
                                                   Math.max(72, tooltipLabel.implicitWidth))
    readonly property point anchorOrigin: targetItem ? targetItem.mapToItem(parent, 0, 0) : Qt.point(0, 0)

    padding: 10
    leftPadding: 12
    rightPadding: 12
    topPadding: 8
    bottomPadding: 8
    implicitWidth: preferredTextWidth + leftPadding + rightPadding
    implicitHeight: tooltipLabel.implicitHeight + topPadding + bottomPadding
    parent: Overlay.overlay
    x: Math.max(8, Math.min(parent.width - width - 8,
                            anchorOrigin.x + (targetItem ? targetItem.width - width : 0) / 2))
    y: anchorOrigin.y > height + 12
       ? anchorOrigin.y - height - 8
       : anchorOrigin.y + (targetItem ? targetItem.height : 0) + 8
    z: 1000

    contentItem: Text {
        id: tooltipLabel
        text: root.text
        width: root.preferredTextWidth
        color: theme.textStrong
        font.family: theme.fontUi
        font.pixelSize: 12
        lineHeight: 1.2
        wrapMode: Text.Wrap
        renderType: Text.NativeRendering
    }

    background: Rectangle {
        radius: 8
        color: theme.surfaceTop
        border.width: 1
        border.color: theme.lineStrong
        layer.enabled: true
        layer.effect: MultiEffect {
            shadowEnabled: true
            shadowColor: "#80000000"
            shadowBlur: 0.28
            shadowVerticalOffset: 3
        }
    }

    Theme { id: theme }
}
