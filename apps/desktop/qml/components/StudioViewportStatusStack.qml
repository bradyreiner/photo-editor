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

    signal retryRequested()

    StatusChip {
        id: previewUpdateBadge
        objectName: statusStack.viewportObjectName.length > 0
                    ? statusStack.viewportObjectName + "PreviewUpdateBadge"
                    : "previewUpdateBadge"
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 14
        anchors.bottomMargin: 14
        width: implicitWidth
        visible: statusStack.pixelInspectionRequested && !statusStack.pixelGridFullResolutionBacked
        text: "Loading full-resolution pixels"
        tone: "accent"
        busy: true
        actionText: ""
        tooltipText: text
        z: 1.4
    }

    StatusChip {
        id: maskActivityStatusChip
        objectName: statusStack.viewportObjectName.length > 0
                    ? statusStack.viewportObjectName + "MaskActivityStatusChip"
                    : "maskActivityStatusChip"
        readonly property string activityAnimationContract: "nonblocking|persistent|animated-dot:v2|progress-strip:true|smart:indeterminate|retouch:determinate"
        anchors.left: parent.left
        anchors.bottom: previewUpdateBadge.visible ? previewUpdateBadge.top : parent.bottom
        anchors.leftMargin: 14
        anchors.bottomMargin: previewUpdateBadge.visible ? 8 : 14
        width: implicitWidth
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
}
