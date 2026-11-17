pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    objectName: "timelineSourceEditBar"
    required property var theme
    required property var documentController
    property var selectedClip: ({})
    readonly property bool sourceReady: documentController
            && documentController.hasSourceClip && documentController.sourcePatchReady
    readonly property bool singleSelection: documentController
            && documentController.currentIndex >= 0 && documentController.selectedClipCount === 1
    readonly property string sourceReason: documentController && documentController.hasSourceClip
            ? "Choose an unlocked destination track in the Source patch controls."
            : "Open a media item in Source and mark its range."
    readonly property string replacementReason: !singleSelection
            ? "Select one timeline clip to replace."
            : !sourceReady ? sourceReason : "The selected clip cannot be replaced."
    signal sourceRequested()
    implicitHeight: commands.implicitHeight
    implicitWidth: commands.implicitWidth
    Accessible.name: "Source editing commands"
    Accessible.role: Accessible.ToolBar

    Flow {
        id: commands
        width: parent.width
        spacing: root.theme.gapDense

        ActionButton {
            objectName: "timelineQuickSourceButton"
            text: "Source"
            iconName: "media"
            compact: true
            tooltipText: "Open Source to preview a clip and mark In and Out points."
            onClicked: root.sourceRequested()
        }
        ActionButton {
            objectName: "timelineQuickInsertSourceButton"
            text: "Insert"
            iconName: "insert"
            compact: true
            enabled: root.sourceReady
            disabledReason: root.sourceReason
            tooltipText: "Insert the marked source range at the playhead and move later clips."
            onClicked: root.documentController.insertSourceClipAtPlayhead()
        }
        ActionButton {
            objectName: "timelineQuickOverwriteSourceButton"
            text: "Overwrite"
            iconName: "overwrite"
            compact: true
            enabled: root.sourceReady
            disabledReason: root.sourceReason
            tooltipText: "Place the marked source range at the playhead, replacing occupied time."
            onClicked: root.documentController.overwriteSourceClipAtPlayhead()
        }
        ActionButton {
            objectName: "timelineQuickReplaceSourceButton"
            text: "Replace"
            iconName: "refresh"
            compact: true
            enabled: root.sourceReady && root.singleSelection
            disabledReason: root.replacementReason
            tooltipText: "Replace the selected timeline clip from Source while preserving its timeline span."
            onClicked: root.documentController.replaceSelectedClipFromSource()
        }
        ActionButton {
            objectName: "timelineQuickFitToFillButton"
            text: "Fit to Fill"
            iconName: "fit"
            compact: true
            enabled: root.sourceReady && root.singleSelection
            disabledReason: root.replacementReason
            tooltipText: "Retime the marked source range to fill the selected clip's timeline duration."
            onClicked: root.documentController.fitToFillSelectedClipFromSource()
        }
        ActionButton {
            objectName: "timelineQuickAppendSourceButton"
            text: "Append"
            iconName: "jump-end"
            compact: true
            enabled: root.sourceReady
            disabledReason: root.sourceReason
            tooltipText: "Add the marked Source range at the end of the sequence."
            onClicked: root.documentController.appendSourceClipToTimeline()
        }
        ActionButton {
            objectName: "timelineQuickMatchFrameButton"
            text: "Match Frame"
            iconName: "media"
            compact: true
            enabled: root.singleSelection
            disabledReason: "Select one media clip to find its original source frame."
            tooltipText: "Open the selected clip's source frame at the sequence playhead."
            onClicked: {
                if (root.documentController.matchFrameToSource())
                    root.sourceRequested()
            }
        }
    }
}
