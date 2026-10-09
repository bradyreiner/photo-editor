import QtQuick
import QtQuick.Layouts

InspectorSlider {
    id: control

    required property Item workspace
    required property var previewController
    property string targetSupport: "base-image,adjustment-layer"
    readonly property string targetSupportContract: workspace.adjustmentControlTargetSupportContract
    readonly property string targetSupportSignature: workspace.adjustmentControlTargetSupportSignature(targetSupport)
    readonly property string studioAdjustmentSliderContract: "studio-adjustment-slider:v2|shared-inspector-slider:true|wide-rail:true|fixed-value:true|fixed-reset:true|modified-marker:false|double-click-reset:true|coalesced-drag:true|target-aware:true|disabled-reason:true|no-elide:true"
    readonly property string studioAdjustmentSliderSignature: studioAdjustmentSliderContract +
                                                             "|target-support:" + targetSupport +
                                                             "|target-supported:" + String(supportsActiveEditTarget) +
                                                             "|slider:" + sliderContract +
                                                             "|target:" + targetSupportSignature
    readonly property bool supportsActiveEditTarget: workspace.adjustmentControlCanWriteActiveTarget(targetSupport)

    Layout.fillWidth: true
    disabledReason: supportsActiveEditTarget ? "" : workspace.adjustmentControlDisabledReason(targetSupport)
    accent: workspace.mint
    enabled: supportsActiveEditTarget
    from: -100
    to: 100
    stepSize: 1

    property real interactionStartValue: 0
    property string interactionPublication: ""
    onInteractionStarted: {
        interactionStartValue = value
        interactionPublication = typeof workspace.editPublicationSignature === "function" ? workspace.editPublicationSignature() : ""
        if (typeof workspace.cancelEditCompletion === "function") workspace.cancelEditCompletion()
        previewController.beginCoalescedEdit()
    }
    onInteractionFinished: {
        previewController.endCoalescedEdit()
        if (Math.abs(value - interactionStartValue) > 0.00001 && typeof workspace.queueEditCompletion === "function")
            workspace.queueEditCompletion(interactionPublication)
    }
    onResetRequested: valueDragged(defaultValue)
}
