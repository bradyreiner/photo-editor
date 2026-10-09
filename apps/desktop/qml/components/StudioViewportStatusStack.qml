pragma ComponentBehavior: Bound
import QtQuick

Item {
    id: statusStack

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

    Column {
        objectName: statusStack.viewportObjectName + "LeftStatusStack"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.bottomMargin: 14
        width: implicitWidth
        height: implicitHeight
        spacing: theme.space12

        StatusChip {
            id: maskActivityStatusChip
            objectName: statusStack.viewportObjectName.length > 0
                        ? statusStack.viewportObjectName + "MaskActivityStatusChip"
                        : "maskActivityStatusChip"
            readonly property string activityAnimationContract: "nonblocking|persistent|animated-dot:v2|progress-strip:true|smart:indeterminate|retouch:determinate"
            width: implicitWidth
            height: implicitHeight
            visible: statusStack.previewContentVisible && statusStack.maskActivityChipVisible
            text: statusStack.maskActivityChipText
            tone: "warning"
            busy: true
            tooltipText: text
            z: 1.45

            ProgressStrip {
                objectName: statusStack.viewportObjectName.length > 0
                            ? statusStack.viewportObjectName + "MaskActivityProgressStrip"
                            : "maskActivityProgressStrip"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 3
                progress: statusStack.maskActivityProgressValue
                accent: theme.primaryAccent
                trackColor: "transparent"
                visible: maskActivityStatusChip.visible
            }
        }

        StatusChip {
            id: previewUpdateBadge
            objectName: statusStack.viewportObjectName.length > 0
                        ? statusStack.viewportObjectName + "PreviewUpdateBadge"
                        : "previewUpdateBadge"
            width: implicitWidth
            height: implicitHeight
            visible: statusStack.pixelInspectionRequested && !statusStack.pixelGridFullResolutionBacked
            text: "Loading full-resolution pixels"
            tone: "accent"
            busy: true
            actionText: ""
            tooltipText: text
            z: 1.4
        }
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
        visible: statusStack.previewContentVisible &&
                 (statusStack.previewRenderFailed ||
                  (statusStack.previewProcessing && statusStack.previewUpdateDelayElapsed))
        text: statusStack.previewUpdateBadgeText
        tone: statusStack.previewRenderFailed ? "error" : "info"
        busy: statusStack.previewProcessing && !statusStack.previewRenderFailed
        actionText: statusStack.previewRenderFailed ? "Retry" : ""
        detailText: statusStack.previewFailureDetail
        tooltipText: statusStack.previewFailureDetail.length > 0
                     ? statusStack.previewUpdateBadgeText + " · " + statusStack.previewFailureDetail
                     : statusStack.previewUpdateBadgeText
        z: 1.5
        onActionTriggered: statusStack.retryRequested()
    }
}
