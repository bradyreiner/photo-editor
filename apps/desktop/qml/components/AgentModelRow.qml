import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root

    required property var modelData
    property bool selected: false
    property bool commandEnabled: true
    property string detailText: ""
    property string statusText: ""
    property color statusColor: theme.textMuted

    signal selectedRequested(string modelId)

    Theme { id: theme }

    readonly property string agentModelRowContract: "agent-model-row:v1|selected-state:true|status-state:true|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|name-wrap:true|detail-wrap:true|status-fit:true|tooltip-full:true|accessibility-full:true"
    readonly property string modelId: String(modelData.id || "")
    readonly property string modelName: String(modelData.name || modelData.id || "Local model")
    readonly property bool actionable: commandEnabled && modelId.length > 0

    implicitHeight: modelRowLayout.implicitHeight + theme.space8
    activeFocusOnTab: actionable
    Accessible.role: Accessible.ListItem
    Accessible.name: modelName
    Accessible.description: detailText
    Accessible.selected: selected

    Keys.onReturnPressed: if (actionable) root.selectedRequested(modelId)
    Keys.onEnterPressed: if (actionable) root.selectedRequested(modelId)
    Keys.onSpacePressed: if (actionable) root.selectedRequested(modelId)

    Rectangle {
        anchors.fill: parent
        radius: theme.radiusControl
        color: root.selected
               ? theme.alpha(theme.focusRing, 0.12)
               : (rowMouse.containsMouse || root.activeFocus
                  ? theme.alpha(theme.ink800, 0.30)
                  : theme.alpha(theme.ink800, 0.20))
        border.width: 1
        border.color: root.activeFocus
                      ? theme.focusRing
                      : (root.selected ? theme.alpha(theme.focusRing, 0.36) : theme.alpha(theme.lineSoft, 0.20))
    }

    RowLayout {
        id: modelRowLayout
        anchors.fill: parent
        anchors.margins: theme.space4
        spacing: theme.space6

        Rectangle {
            Layout.preferredWidth: 7
            Layout.preferredHeight: 7
            radius: 4
            color: root.statusColor
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                Layout.fillWidth: true
                text: root.modelName
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }

            Text {
                Layout.fillWidth: true
                text: root.detailText
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        Text {
            Layout.preferredWidth: 82
            text: root.statusText
            color: root.statusColor
            font.family: theme.fontMono
            font.pixelSize: 9
            horizontalAlignment: Text.AlignRight
            verticalAlignment: Text.AlignVCenter
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 7
            elide: Text.ElideNone
        }
    }

    MouseArea {
        id: rowMouse
        anchors.fill: parent
        enabled: root.actionable
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.selectedRequested(root.modelId)
    }

    ToolTip.visible: rowMouse.containsMouse
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: root.modelName + "\n" + root.detailText
}
