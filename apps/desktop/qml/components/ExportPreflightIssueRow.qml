import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    property var issue: ({})
    property bool showTextResolutionActions: false
    property bool showInspectAction: showTextResolutionActions
    property bool showIgnoreAction: severity !== "error"
    property bool currentIssue: false
    signal inspectRequested()
    signal resizeFrameRequested()
    signal fitTextRequested()
    signal ignoreRequested()
    readonly property string severity: String(issue.severity || "warning")
    readonly property string message: String(issue.message || "")
    readonly property string tone: severity === "error" ? "error" : "warning"
    readonly property string label: severity === "error" ? "Blocking" : "Check"
    readonly property string exportPreflightIssueRowContract: "export-preflight-issue-row:v1|severity-chip:true|severity-role:" +
                                                              root.tone +
                                                              "|message-wrap:true|readable-text:true|no-elide:true|shared-tone:true|compact-failure:true|tooltip-full:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|message-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string statusSurfaceContract: "status-surface:v1|preflight:blocking-warning|compact-row:true|status-chip:true|message-after-status:true"
    readonly property string textResolutionContract: "text-preflight-actions:v1|inspect:true|resize-frame:true|fit-text:true|ignore:true|keyboard:true|no-elide:true"

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: issueRow.implicitHeight + theme.space12
    radius: theme.radiusControl
    color: tone === "error"
           ? theme.alpha(theme.error, 0.10)
           : theme.alpha(theme.warning, 0.10)
    border.width: root.currentIssue ? 2 : theme.chromeBorderWidth
    border.color: root.currentIssue ? theme.focusRing : tone === "error"
                  ? theme.alpha(theme.error, 0.42)
                  : theme.alpha(theme.warning, 0.36)
    clip: true
    Accessible.name: label + ": " + message
    Accessible.role: Accessible.StaticText

    ToolTip.visible: issueHover.hovered && issueText.contentHeight > issueText.height + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: message

    HoverHandler { id: issueHover }

    ColumnLayout {
        id: issueRow
        anchors.fill: parent
        anchors.margins: theme.space6
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            StatusChip {
                text: root.label
                tone: root.tone
                compact: true
                detailText: root.message
                tooltipText: root.label + " preflight issue."
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
                Layout.alignment: Qt.AlignTop
            }

            Label {
                id: issueText
                objectName: root.objectName.length ? root.objectName + "Message" : ""
                Layout.fillWidth: true
                text: root.message
                color: root.tone === "error" ? theme.textStrong : theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
                verticalAlignment: Text.AlignVCenter
            }
        }

        Flow {
            Layout.fillWidth: true
            Layout.preferredHeight: childrenRect.height
            visible: root.showInspectAction || root.showTextResolutionActions || root.showIgnoreAction
            spacing: theme.space6

            ActionButton {
                objectName: root.objectName.length ? root.objectName + "Inspect" : ""
                text: "Inspect"
                compact: true
                visible: root.showInspectAction
                onClicked: root.inspectRequested()
            }

            ActionButton {
                text: "Resize Frame"
                compact: true
                visible: root.showTextResolutionActions
                onClicked: root.resizeFrameRequested()
            }

            ActionButton {
                text: "Fit Text"
                compact: true
                visible: root.showTextResolutionActions
                onClicked: root.fitTextRequested()
            }

            ActionButton {
                text: "Ignore"
                compact: true
                visible: root.showIgnoreAction
                onClicked: root.ignoreRequested()
            }
        }
    }
}
