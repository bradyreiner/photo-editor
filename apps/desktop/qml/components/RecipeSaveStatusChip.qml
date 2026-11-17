pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root
    required property bool savePending
    required property string saveError
    property int recoveryCount: 0
    property string recoveryDetail: ""
    property var recoveryEntries: []
    property var archiveRecoveryHandler: null
    readonly property bool olderCopiesOnly: recoveryEntries.length > 0 &&
        recoveryEntries.every(function(entry) { return entry.conflict === true })

    objectName: "recipeSaveStatusChip"
    visible: root.savePending || root.saveError.length > 0 || root.recoveryCount > 0
    readonly property string text: root.saveError.length > 0 ? "Edits not safely saved"
                                  : root.savePending ? "Saving edits"
                                  : root.olderCopiesOnly ? root.recoveryCount + (root.recoveryCount === 1 ? " older edit copy" : " older edit copies")
                                  : root.recoveryCount === 1 ? "1 photo needs recovery"
                                  : root.recoveryCount + " photos need recovery"
    readonly property string tone: root.saveError.length > 0 ? "error"
                                   : root.recoveryCount > 0 ? "warning" : "neutral"
    readonly property bool busy: root.savePending && root.saveError.length === 0
    readonly property string tooltipText: root.saveError.length > 0
                 ? root.saveError
                 : !root.savePending && root.recoveryCount > 0 ? root.recoveryDetail
                 : "Pending edits are checkpointed locally while the catalog save completes."
    readonly property string detailText: tooltipText
    implicitWidth: 18
    implicitHeight: 18
    Accessible.role: Accessible.ProgressBar
    Accessible.name: root.text
    Accessible.description: root.detailText
    Accessible.ignored: !root.busy

    Theme { id: theme }

    OpticalActivityIndicator {
        objectName: "recipeSaveSpinner"
        anchors.fill: parent
        visible: root.busy
        active: visible
    }
    StatusChip {
        objectName: "recipeSaveError"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        visible: root.saveError.length > 0 || (!root.busy && root.recoveryCount > 0)
        text: root.text
        tone: root.tone
        tooltipText: root.tooltipText
        detailText: root.tooltipText
        actionText: root.saveError.length === 0 && root.recoveryCount > 0 ? "Review" : ""
        onActionTriggered: reviewDialog.openReview()
    }

    RecoveryReviewDialog {
        id: reviewDialog
        recoveryDetail: root.recoveryDetail
        recoveryEntries: root.recoveryEntries
        archiveRecoveryHandler: root.archiveRecoveryHandler
    }
}
