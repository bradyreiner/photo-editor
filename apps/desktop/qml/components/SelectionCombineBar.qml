pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property string operation: "add"
    property bool hasSelection: false
    property bool objectPicking: false
    property bool showTips: true
    signal operationRequested(string operation)
    Theme { id: theme }
    readonly property var modes: [
        {key: "add", name: qsTr("Add"), icon: "selection-add", objectName: "maskCombineAddButton", hint: qsTr("Keep your selection and include the next area.")},
        {key: "subtract", name: qsTr("Remove"), icon: "selection-subtract", objectName: "maskCombineSubtractButton", hint: qsTr("Cut the next area out of your selection.")},
        {key: "intersect", name: qsTr("Overlap"), icon: "selection-intersect", objectName: "maskCombineIntersectButton", hint: qsTr("Keep only the area shared by both selections.")}
    ]
    spacing: theme.space4
    Label {
        text: qsTr("Selection mode")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        font.weight: theme.weightControl
    }
    GridLayout {
        id: modeGrid
        Layout.fillWidth: true
        columns: root.width >= theme.scaled(360) ? 3 : 2
        columnSpacing: theme.space2
        rowSpacing: theme.space2
        Repeater {
            model: root.modes
            delegate: MiniButton {
                id: modeButton
                required property var modelData
                objectName: modeButton.modelData.objectName
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.columnSpan: modeButton.modelData.key === "intersect" && modeGrid.columns === 2 ? 2 : 1
                text: modeButton.modelData.name
                iconName: modeButton.modelData.icon
                showLeadingIcon: true
                role: "segmented"
                active: root.operation === modeButton.modelData.key
                tooltip: modeButton.modelData.hint
                Accessible.description: modeButton.modelData.hint
                onClicked: root.operationRequested(modeButton.modelData.key)
            }
        }
    }
    Label {
        objectName: "selectionCombineHint"
        Layout.fillWidth: true
        visible: root.showTips
        text: !root.hasSelection ? qsTr("No selection yet. Your next gesture starts one.")
            : (root.modes.find(m => m.key === root.operation) || root.modes[0]).hint
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
}
