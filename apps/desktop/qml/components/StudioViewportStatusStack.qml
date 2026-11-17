pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: statusStack
    // The viewport's pan/zoom and painting surfaces accept pointer presses.
    // Status actions must be above them while empty status space stays inert.
    z: 100

    required property string viewportObjectName
    required property bool pixelInspectionRequested
    required property bool pixelGridFullResolutionBacked
    required property bool previewProcessing
    required property bool previewContentVisible
    required property bool editedPreviewSurface
    required property bool previewRenderFailed
    required property string previewUpdateBadgeText
    required property string previewFailureDetail
    required property real maskActivityProgressValue
    required property bool maskActivityChipVisible
    required property string maskActivityChipText
    property bool savePending: false
    property string saveError: ""
    property int saveRecoveryCount: 0
    property string saveRecoveryDetail: ""
    property var saveRecoveryEntries: []
    property var archiveRecoveryHandler: null
    property bool previewUpdateDelayElapsed: false

    signal retryRequested()

    onPreviewProcessingChanged: {
        if (previewProcessing) {
            previewUpdateDelayElapsed = false
            previewUpdateDelay.restart()
        } else {
            previewUpdateDelay.stop()
            previewUpdateDelayElapsed = false
        }
    }

    Component.onCompleted: {
        if (previewProcessing)
            previewUpdateDelay.restart()
    }

    Timer {
        id: previewUpdateDelay
        interval: 320
        repeat: false
        onTriggered: statusStack.previewUpdateDelayElapsed = true
    }

    Theme { id: theme }

    OpticalActivityIndicator {
        objectName: statusStack.viewportObjectName + "LoadingSpinner"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 14
        width: theme.scaled(18)
        height: width
        visible: !statusStack.previewRenderFailed && statusStack.saveError.length === 0 &&
                 ((statusStack.previewContentVisible &&
                   (statusStack.maskActivityChipVisible ||
                    (statusStack.previewProcessing && statusStack.previewUpdateDelayElapsed))) ||
                  (statusStack.pixelInspectionRequested && !statusStack.pixelGridFullResolutionBacked) ||
                  (statusStack.editedPreviewSurface && statusStack.savePending))
        active: visible
        Accessible.name: statusStack.maskActivityChipVisible ? statusStack.maskActivityChipText
                         : statusStack.pixelInspectionRequested && !statusStack.pixelGridFullResolutionBacked
                           ? "Loading full-resolution pixels"
                           : statusStack.previewProcessing ? statusStack.previewUpdateBadgeText : "Saving edits"
    }

    RecipeSaveStatusChip {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 14
        anchors.bottomMargin: statusStack.saveError.length === 0 && statusStack.saveRecoveryCount > 0 ? 48 : 14
        width: implicitWidth
        height: implicitHeight
        savePending: false
        saveError: statusStack.editedPreviewSurface ? statusStack.saveError : ""
        recoveryCount: statusStack.editedPreviewSurface ? statusStack.saveRecoveryCount : 0
        recoveryDetail: statusStack.saveRecoveryDetail
        recoveryEntries: statusStack.saveRecoveryEntries
        archiveRecoveryHandler: statusStack.archiveRecoveryHandler
    }

    StatusChip {
        id: previewRenderStatusChip
        objectName: statusStack.viewportObjectName.length > 0
                    ? statusStack.viewportObjectName + "PreviewRenderStatusChip"
                    : "previewRenderStatusChip"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 14
        anchors.bottomMargin: 14
        width: implicitWidth
        visible: statusStack.previewContentVisible && statusStack.previewRenderFailed && statusStack.saveError.length === 0
        text: statusStack.previewUpdateBadgeText
        tone: statusStack.previewRenderFailed ? "error" : "info"
        busy: false
        actionText: statusStack.previewRenderFailed ? "Retry" : ""
        detailText: statusStack.previewFailureDetail
        tooltipText: statusStack.previewFailureDetail.length > 0
                     ? statusStack.previewUpdateBadgeText + " · " + statusStack.previewFailureDetail
                     : statusStack.previewUpdateBadgeText
        z: 1.5
        onActionTriggered: statusStack.retryRequested()
    }
}
