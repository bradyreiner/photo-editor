import QtQuick

NavigationTab {
    id: root
    property string label: ""
    property string tabKey: ""
    signal selectedRequested(string tabKey)
    text: root.label
    tooltipText: root.label
    onClicked: root.selectedRequested(root.tabKey)

    readonly property string editInspectorTabButtonContract: "edit-inspector-tab-button:v1|selected-state:true|hover:true|keyboard:true|width-to-label:true|readable-text:true|no-elide:true"
    readonly property string editInspectorTabVisualContract: "edit-inspector-tab-visual:v3|pressed:true|focus-ring:2px|target:density-aware-control|selection:neutral-fill-and-weight|text-minimum:10px|motion:reduced-motion-aware|corners:3px"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|label-width:true|label-fit:true|minimum-px:10|tooltip-full:true|accessibility-full:true"

}
