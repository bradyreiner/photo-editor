pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

ComboBox {
    id: root

    property color accent: theme.accentWarm400
    property bool fitTextToWidth: true
    property bool showFullTextTooltip: true

    Theme { id: theme }

    implicitHeight: theme.controlHeight
    implicitWidth: Math.max(theme.menuMinWidth, selectedLabel.implicitWidth + theme.space24 + theme.space16)
    font.family: theme.fontUi
    font.pixelSize: 11
    hoverEnabled: true
    activeFocusOnTab: true
    ToolTip.visible: root.hovered && root.showFullTextTooltip && selectedLabel.contentWidth > selectedLabel.width + 1
    ToolTip.delay: 250
    ToolTip.timeout: 3000
    ToolTip.text: root.displayText

    delegate: ItemDelegate {
        id: optionDelegate
        required property int index
        required property var modelData
        width: root.width
        highlighted: root.highlightedIndex === index
        ToolTip.visible: optionDelegate.hovered && root.showFullTextTooltip && optionLabel.contentWidth > optionLabel.width + 1
        ToolTip.delay: 250
        ToolTip.timeout: 3000
        ToolTip.text: String(optionDelegate.modelData)

        contentItem: Text {
            id: optionLabel
            text: optionDelegate.modelData
            color: optionDelegate.highlighted ? theme.textStrong : theme.textMain
            font.family: root.font.family
            font.pixelSize: 11
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
            minimumPixelSize: 9
            clip: true
            leftPadding: theme.space12
            rightPadding: theme.space12
        }

        background: Rectangle {
            color: optionDelegate.highlighted ? theme.alpha(root.accent, 0.16) : theme.alpha(theme.glassBase, 0.9)
        }
    }

    indicator: AppIcon {
        name: "chevron-right"
        color: root.enabled ? theme.textFaint : theme.alpha(theme.textMuted, 0.62)
        rotation: 90
        width: 10
        height: 10
        anchors.right: parent.right
        anchors.rightMargin: theme.space12
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        id: selectedLabel
        text: root.displayText
        color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
        leftPadding: theme.space12
        rightPadding: theme.space24 + theme.space4
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
        minimumPixelSize: 9
        clip: true
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: root.enabled ? theme.alpha(theme.glassBase, 0.38) : theme.alpha(theme.glassBase, 0.26)
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus
                      ? theme.alpha(theme.focusRing, 0.9)
                      : (root.hovered ? theme.alpha(root.accent, 0.34) : theme.alpha(theme.glassEdge, 0.2))

        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            radius: Math.max(1, parent.radius - 1)
            visible: root.visualFocus
            color: theme.alpha(theme.focusFill, 0.2)
        }
    }

    popup: Popup {
        y: root.height + theme.space4
        width: Math.max(root.width, theme.menuMinWidth)
        padding: theme.space4

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: Rectangle {
            radius: theme.radiusPanel
            color: theme.alpha(theme.glassBase, 0.96)
            border.width: 1
            border.color: theme.alpha(theme.glassEdge, 0.28)
        }
    }
}
