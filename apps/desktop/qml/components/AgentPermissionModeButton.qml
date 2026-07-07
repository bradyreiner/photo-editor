import QtQuick
import QtQuick.Controls

ActionButton {
    id: root

    property string mode: ""
    property string selectedMode: ""
    property string helperText: ""

    signal modeSelected(string mode)

    readonly property string agentPermissionModeButtonContract: "agent-permission-mode-button:v1|selected-state:true|mode-signal:true|tooltip-full:true|keyboard:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    compact: true
    active: selectedMode === mode
    iconName: active ? "check" : "adjust"
    tooltipText: helperText.length > 0 ? helperText : text
    showFullTextTooltip: true
    Accessible.name: text
    Accessible.description: tooltipText

    onClicked: root.modeSelected(root.mode)
}
