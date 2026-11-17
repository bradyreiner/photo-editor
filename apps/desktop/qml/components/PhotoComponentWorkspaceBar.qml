import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: root

    required property var orchestrator
    property var workspace: null
    readonly property bool active: !!root.orchestrator && root.orchestrator.active === true
    readonly property bool busy: !root.orchestrator || root.orchestrator.busy === true
    readonly property bool textSessionActive: !!root.workspace && root.workspace.textEditSessionActive === true
    readonly property bool pendingCanvasDraft: !!root.workspace && root.workspace.componentWorkspaceHasPendingCanvasDraft === true
    readonly property bool canApply: !root.busy && (root.orchestrator.canApply === true || root.textSessionActive || root.pendingCanvasDraft)
    readonly property var breadcrumbs: root.orchestrator ? (root.orchestrator.breadcrumbs || []) : []
    readonly property string errorText: String((root.orchestrator ? root.orchestrator.error || "" : "") ||
        (root.workspace ? root.workspace.componentWorkspaceApplyBlockReason || "" : ""))
    readonly property string accessibilityContract: "photo-component-workspace-bar:v1|actions:apply-cancel|breadcrumbs:explicit-depth|busy:opening-or-apply|error:inline|narrow:wrap"

    Theme { id: theme }

    visible: root.active
    implicitHeight: content.implicitHeight + theme.space12 * 2
    color: theme.alpha(theme.ink950, 0.96)
    radius: theme.radiusPanel
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(theme.primaryAccent, 0.32)

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: theme.space12
        anchors.rightMargin: theme.space12
        anchors.topMargin: theme.space8
        anchors.bottomMargin: theme.space8
        spacing: theme.space4

        Flow {
            Layout.fillWidth: true
            spacing: theme.space6
            flow: Flow.LeftToRight

            Label {
                objectName: "photoComponentWorkspaceTitle"
                text: qsTr("Component contents")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                font.weight: Font.DemiBold
                verticalAlignment: Text.AlignVCenter
                height: theme.controlHeight
            }

            Repeater {
                model: root.breadcrumbs
                delegate: Row {
                    required property int index
                    required property var modelData
                    readonly property int breadcrumbDepth: Math.max(0, Number(modelData && modelData.depth !== undefined ? modelData.depth : index))
                    spacing: theme.space4
                    height: theme.controlHeight

                    Item {
                        width: Math.min(3, breadcrumbDepth) * theme.space4
                        height: 1
                        visible: breadcrumbDepth > 0
                    }
                    Label {
                        visible: index > 0
                        text: "›"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        verticalAlignment: Text.AlignVCenter
                    }
                    Label {
                        objectName: "photoComponentWorkspaceBreadcrumb"
                        text: String(modelData && modelData.name !== undefined ? modelData.name : "")
                        width: Math.min(implicitWidth, Math.max(theme.scaled(76),
                            (root.width - theme.scaled(240)) / Math.max(1, root.breadcrumbs.length)))
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        Accessible.description: qsTr("Component nesting level %1").arg(breadcrumbDepth + 1)
                    }
                }
            }

            OpticalActivityIndicator {
                objectName: "photoComponentWorkspaceBusyIndicator"
                width: theme.scaled(16)
                height: theme.scaled(16)
                visible: root.busy
                active: visible
            }

            Label {
                objectName: "photoComponentWorkspaceBusyLabel"
                text: root.busy ? qsTr("Preparing component…")
                     : root.textSessionActive ? qsTr("Finish active text with Done, or use Apply to finish it first.") : ""
                visible: root.busy || root.textSessionActive
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                verticalAlignment: Text.AlignVCenter
                height: theme.controlHeight
            }

            Item { width: theme.space2; height: 1 }

            ActionButton {
                objectName: "photoComponentWorkspaceCancelButton"
                text: qsTr("Cancel")
                compact: true
                tone: "secondary"
                onClicked: root.orchestrator.cancel()
            }

            ActionButton {
                objectName: "photoComponentWorkspaceApplyButton"
                text: qsTr("Apply")
                compact: true
                tone: "primary"
                enabled: root.canApply
                disabledReason: root.busy ? qsTr("Wait for the component operation to finish.")
                                           : qsTr("The component preview is not ready to apply.")
                onClicked: {
                    if (root.workspace && typeof root.workspace.resolveComponentWorkspaceEdit === "function") {
                        if (!root.workspace.resolveComponentWorkspaceEdit()) return
                    } else if (root.textSessionActive &&
                            (!root.workspace || typeof root.workspace.resolveTextSession !== "function" ||
                             !root.workspace.resolveTextSession()))
                        return
                    root.orchestrator.apply()
                }
            }
        }

        Label {
            objectName: "photoComponentWorkspaceError"
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: theme.error
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
        }
    }
}
