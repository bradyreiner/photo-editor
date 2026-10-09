pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    objectName: "libraryDeliveryPanel"
    required property var jobQueue
    property var assetIds: []
    property bool expanded: false

    signal exportSelectedRequested()

    Theme { id: theme }

    readonly property var projectJobs: {
        if (!root.jobQueue)
            return []
        root.jobQueue.jobCount
        const ids = root.normalizedAssetIds
        if (ids.length === 0)
            return []
        const idSet = Object.create(null)
        for (let index = 0; index < ids.length; ++index) {
            const id = String(ids[index] || "").trim()
            if (id.length > 0)
                idSet[id] = true
        }
        return root.jobQueue.jobsForCenter("asset_export").filter(function(job) {
            return Boolean(idSet[String(job.assetId || "")])
        }).sort(function(left, right) {
            return new Date(String(right.updatedAt || right.createdAt || 0)).getTime()
                    - new Date(String(left.updatedAt || left.createdAt || 0)).getTime()
        })
    }
    readonly property int completedCount: countStatus("Completed")
    readonly property int failedCount: countStatus("Failed")
    readonly property int queuedCount: countStatus("Queued")
    readonly property int runningCount: countStatus("Running")
                       + countStatus("Pausing") + countStatus("Canceling")
    readonly property var normalizedAssetIds: Array.from(root.assetIds || [])
    readonly property var failedJobs: projectJobs.filter(function(job) {
        return String(job.status || "") === "Failed"
    }).slice(0, 3)
    readonly property var lastVerifiedJob: {
        for (let index = 0; index < projectJobs.length; ++index) {
            const job = projectJobs[index]
            const summary = job.metadata && job.metadata.deliverySummary
                    ? job.metadata.deliverySummary : ({})
            const outputPath = String(job.outputPath || "").trim()
            if (String(job.status || "") === "Completed"
                    && outputPath.length > 0
                    && String(summary.outputPath || "") === outputPath
                    && summary.artifactVerified === true) {
                return job
            }
        }
        return ({})
    }

    function countStatus(status) {
        let count = 0
        const jobs = projectJobs
        for (let index = 0; index < jobs.length; ++index) {
            if (String(jobs[index].status || "") === status)
                count += 1
        }
        return count
    }

    function fileName(path) {
        const parts = String(path || "").split(/[\\/]/)
        return parts.length > 0 ? parts[parts.length - 1] : ""
    }

    implicitHeight: panelColumn.implicitHeight
    implicitWidth: 260

    Rectangle {
        anchors.fill: parent
        radius: theme.radiusControl
        color: theme.alpha(theme.ink950, 0.58)
        border.width: theme.hairlineWidth
        border.color: theme.alpha(theme.lineSoft, 0.34)
    }

    ColumnLayout {
        id: panelColumn
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        anchors.topMargin: theme.space6
        anchors.bottomMargin: theme.space6
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space6

            AppIcon {
                Layout.preferredWidth: theme.scaled(15)
                Layout.preferredHeight: theme.scaled(15)
                name: "export"
                color: theme.textMuted
            }

            Label {
                Layout.fillWidth: true
                text: "Delivery"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeBody
                font.weight: Font.DemiBold
            }

            RowLayout {
                objectName: "libraryDeliveryCollapsedSummary"
                Layout.maximumWidth: theme.scaled(118)
                visible: !root.expanded
                spacing: theme.space4
                Accessible.role: Accessible.StaticText
                Accessible.name: root.completedCount + " completed, " + root.failedCount
                                + " failed, " + root.queuedCount + " queued exports"

                AppIcon {
                    Layout.preferredWidth: theme.scaled(11)
                    Layout.preferredHeight: theme.scaled(11)
                    name: "check"
                    color: root.completedCount > 0 ? theme.success : theme.textFaint
                }
                Label {
                    text: root.completedCount
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                    Accessible.name: root.completedCount + " completed"
                }
                AppIcon {
                    Layout.preferredWidth: theme.scaled(11)
                    Layout.preferredHeight: theme.scaled(11)
                    name: "error"
                    color: root.failedCount > 0 ? theme.error : theme.textFaint
                }
                Label {
                    text: root.failedCount
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                    Accessible.name: root.failedCount + " failed"
                }
                AppIcon {
                    Layout.preferredWidth: theme.scaled(11)
                    Layout.preferredHeight: theme.scaled(11)
                    name: "clock"
                    color: root.queuedCount > 0 ? theme.accentCool400 : theme.textFaint
                }
                Label {
                    text: root.queuedCount
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                    Accessible.name: root.queuedCount + " queued"
                }
            }

            ActionButton {
                objectName: "libraryDeliveryExpandButton"
                Layout.preferredWidth: theme.scaled(30)
                Layout.preferredHeight: theme.scaled(30)
                compact: true
                showText: false
                iconName: root.expanded ? "chevron-down" : "chevron-right"
                tone: "quiet"
                tooltipText: root.expanded ? "Hide delivery details" : "Show delivery details"
                accessibleName: tooltipText
                onClicked: root.expanded = !root.expanded
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: theme.space8

            Label {
                text: root.completedCount + " done"
                color: root.completedCount > 0 ? theme.success : theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                Accessible.name: root.completedCount + " completed exports"
            }
            Label {
                text: root.failedCount + " failed"
                color: root.failedCount > 0 ? theme.error : theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                Accessible.name: root.failedCount + " failed exports"
            }
            Label {
                text: root.queuedCount + " queued"
                color: root.queuedCount > 0 ? theme.accentCool400 : theme.textFaint
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
                Accessible.name: root.queuedCount + " queued exports"
            }
            Label {
                visible: root.runningCount > 0
                text: root.runningCount + " running"
                color: theme.accentWarm400
                font.family: theme.fontMono
                font.pixelSize: theme.typeMicro
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: theme.hairlineWidth
            visible: root.expanded
            color: theme.alpha(theme.lineSoft, 0.26)
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: theme.space6

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space2

                Label {
                    text: "LAST VERIFIED OUTPUT"
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeMicro
                    font.letterSpacing: 0.35
                }

                Label {
                    Layout.fillWidth: true
                    text: root.lastVerifiedJob.outputPath
                          ? root.fileName(root.lastVerifiedJob.outputPath)
                          : (root.completedCount > 0 ? "No verified output path" : "No completed output yet")
                    color: root.lastVerifiedJob.outputPath ? theme.textMain : theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: root.lastVerifiedJob.outputPath ? Font.Medium : Font.Normal
                    maximumLineCount: 1
                    elide: Text.ElideMiddle
                    Accessible.name: root.lastVerifiedJob.outputPath
                            ? "Verified output " + String(root.lastVerifiedJob.outputPath)
                            : text
                }
            }

            ActionButton {
                objectName: "libraryDeliveryRevealOutputButton"
                visible: Boolean(root.lastVerifiedJob.outputPath)
                enabled: visible && Boolean(root.jobQueue)
                compact: true
                text: "Show folder"
                tone: "secondary"
                iconName: "folder"
                tooltipText: "Reveal the verified export in its folder"
                accessibleName: tooltipText
                onClicked: {
                    const jobId = String(root.lastVerifiedJob.jobId || "")
                    const outputPath = String(root.lastVerifiedJob.outputPath || "")
                    if (jobId.length > 0 && outputPath.length > 0)
                        root.jobQueue.revealJobOutput(jobId)
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: root.expanded && root.failedJobs.length > 0
            spacing: theme.space4

            Repeater {
                model: root.failedJobs

                delegate: RowLayout {
                    id: failedRow
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: theme.space6

                    AppIcon {
                        Layout.preferredWidth: theme.scaled(14)
                        Layout.preferredHeight: theme.scaled(14)
                        name: "error"
                        color: theme.error
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: theme.space2

                        Label {
                            Layout.fillWidth: true
                            text: String(failedRow.modelData.displayLabel || "Export")
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            maximumLineCount: 1
                            elide: Text.ElideRight
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(failedRow.modelData.errorMessage || "Export failed.")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }

                    ActionButton {
                        objectName: "libraryDeliveryRetryButton"
                        compact: true
                        text: "Retry"
                        iconName: "retry"
                        tone: "secondary"
                        enabled: Boolean(root.jobQueue && root.jobQueue.historyMutationsEnabled)
                        disabledReason: "Job history is read-only while recovery is unresolved."
                        tooltipText: "Retry this failed export"
                        onClicked: root.jobQueue.retryJob(String(failedRow.modelData.jobId || ""))
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.expanded
            spacing: theme.space6

            Label {
                Layout.fillWidth: true
                visible: root.projectJobs.length === 0
                text: root.normalizedAssetIds.length === 0
                      ? "Assign assets to this shoot to track deliveries."
                      : "No export receipts for these assets yet."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                objectName: "libraryDeliveryExportSelectedButton"
                visible: root.normalizedAssetIds.length > 0
                compact: true
                text: "Export selected"
                iconName: "export"
                tone: "primary"
                tooltipText: "Open export for the assets in this shoot"
                onClicked: root.exportSelectedRequested()
            }
        }
    }
}
