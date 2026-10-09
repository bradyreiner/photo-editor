import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: group

    default property alias content: body.data
    property string title: ""
    property bool expanded: true
    property bool headerVisible: true
    property string resetObjectName: ""
    property bool resetVisible: false
    property string compareObjectName: ""
    property bool compareVisible: false
    property bool compareActive: false
    signal compareRequested()
    signal compareHoldRequested(bool held)
    onVisibleChanged: {
        if (!visible && compareActive)
            compareRequested()
    }
    signal toggled()
    signal resetRequested()

    Theme { id: theme }

    Layout.fillWidth: true
    spacing: theme.gapDense

    Rectangle {
        visible: group.headerVisible
        Layout.fillWidth: true
        Layout.preferredHeight: theme.controlHeightDense
        color: headerMouse.containsMouse ? theme.editorControlHover : "transparent"
        border.width: theme.chromeBorderWidth
        border.color: headerMouse.containsMouse
                      ? theme.alpha(theme.accentWarm400, 0.30)
                      : "transparent"
        radius: theme.radiusPanel

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 2
            radius: 1
            color: "transparent"
        }

        MouseArea {
            id: headerMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: group.toggled()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: theme.space8
            anchors.rightMargin: theme.space8
            spacing: theme.space6

            AppIcon {
                name: "chevron-right"
                rotation: group.expanded ? 90 : 0
                color: theme.textMuted
                Layout.preferredWidth: 12
                Layout.preferredHeight: 12
            }

            Label {
                Layout.fillWidth: true
                text: group.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeLabel
                font.weight: theme.weightSection
                font.letterSpacing: 0
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: theme.typeCaption
                elide: Text.ElideNone
            }

            ActionButton {
                objectName: group.compareObjectName
                visible: group.compareVisible
                Layout.preferredWidth: theme.controlHeightDense
                Layout.preferredHeight: theme.controlHeightDense
                showText: false
                iconName: "compare"
                compact: true
                embedded: true
                active: group.compareActive
                tooltipText: "Compare " + group.title + " on this target only. Click to toggle; hold temporarily."
                property bool heldComparison: false
                property bool suppressComparisonClick: false
                onPressed: suppressComparisonClick = false
                onPressAndHold: {
                    heldComparison = true
                    suppressComparisonClick = true
                    group.compareHoldRequested(true)
                }
                onReleased: {
                    if (heldComparison) {
                        heldComparison = false
                        group.compareHoldRequested(false)
                    }
                }
                onCanceled: {
                    if (heldComparison) {
                        heldComparison = false
                        group.compareHoldRequested(false)
                    }
                }
                onClicked: if (!suppressComparisonClick) group.compareRequested()
            }

            MiniButton {
                objectName: group.resetObjectName
                visible: group.resetVisible
                text: "Reset"
                iconName: "reset"
                square: true
                role: "icon"
                tooltip: "Reset this group."
                onClicked: group.resetRequested()
            }
        }
    }

    ColumnLayout {
        id: body
        Layout.fillWidth: true
        spacing: theme.space4
        visible: group.expanded
    }
}
