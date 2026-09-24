pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: root

    property color accent: theme.accentWarm400
    property bool fitTextToWidth: true
    property bool showFullTextTooltip: true

    Theme { id: theme }

    implicitHeight: theme.controlHeight
    implicitWidth: Math.max(theme.menuMinWidth, selectedLabel.implicitWidth + theme.space24 + theme.space16)
    font.family: theme.fontUi
    font.pixelSize: theme.typeBody
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
        implicitHeight: theme.controlHeightDense
        padding: 0
        topPadding: theme.space4
        bottomPadding: theme.space4
        width: root.popup.width - root.popup.leftPadding - root.popup.rightPadding
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
            font.pixelSize: theme.typeBody
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
            minimumPixelSize: 10
            clip: true
            leftPadding: theme.space12
            rightPadding: theme.space12
        }

        background: Rectangle {
            color: optionDelegate.highlighted ? theme.ink750 : "transparent"
        }
    }

    indicator: AppIcon {
        name: "chevron-right"
        color: root.enabled ? theme.textFaint : theme.alpha(theme.textMuted, 0.62)
        rotation: 90
        width: theme.scaled(10)
        height: theme.scaled(10)
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
        minimumPixelSize: 10
        clip: true
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled ? theme.alpha(theme.fieldFill, 0.45)
               : root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: root.visualFocus ? theme.focusBorderWidth : 0
        border.color: theme.focusRing
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
            radius: theme.radiusControl
            color: theme.ink850
            border.width: 1
            border.color: theme.alpha(theme.glassEdge, 0.28)
        }
    }
}
