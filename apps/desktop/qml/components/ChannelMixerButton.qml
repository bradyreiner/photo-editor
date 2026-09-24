import QtQuick
import QtQuick.Controls.Basic

ActionButton {
    id: root

    property string actionValue: ""
    property string helperText: ""
    property bool iconOnlyAction: false

    signal actionRequested(string actionValue)

    readonly property string channelMixerButtonContract: "channel-mixer-button:v2|preset-reset:true|keyboard:true|tooltip-full:true|readable-text:true|no-elide:true|action-value:true|icon-only-reset:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"

    compact: true
    iconName: iconOnlyAction ? "reset" : ""
    showText: !iconOnlyAction
    showFullTextTooltip: true
    tooltipText: helperText.length > 0 ? helperText : text
    Accessible.name: iconOnlyAction && helperText.length > 0 ? helperText : text
    Accessible.description: tooltipText

    onClicked: root.actionRequested(root.actionValue)
}
