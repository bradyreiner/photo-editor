pragma ComponentBehavior: Bound
import QtQuick

StatusChip {
    id: root
    required property bool savePending
    required property string saveError

    objectName: "recipeSaveStatusChip"
    visible: root.savePending || root.saveError.length > 0
    text: root.saveError.length > 0 ? "Edits not safely saved" : "Saving edits"
    tone: root.saveError.length > 0 ? "error" : "neutral"
    busy: root.savePending && root.saveError.length === 0
    tooltipText: root.saveError.length > 0
                 ? root.saveError
                 : "Pending edits are checkpointed locally while the catalog save completes."
    detailText: tooltipText
}
