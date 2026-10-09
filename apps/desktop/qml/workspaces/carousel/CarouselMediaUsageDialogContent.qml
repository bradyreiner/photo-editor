pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselMediaUsageDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2

            Label {
                text: "Media usage"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 20
                font.weight: Font.DemiBold
            }

            Label {
                Layout.fillWidth: true
                text: "Every source, crop reuse, offline dependency, and dependent board in this Carousel."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        ActionButton {
            compact: true
            text: "Refresh"
            onClicked: root.mediaUsageReportState = carouselDocument.mediaUsageReport(
                           carouselDocument.mediaAutofillPlan || [])
        }

        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Close media usage"
            onClicked: carouselMediaUsageDialog.close()
        }
    }

    GridLayout {
        Layout.fillWidth: true
        columns: 5
        columnSpacing: 5
        rowSpacing: 5

        Repeater {
            model: [
                { label: "Sources", value: Number(root.mediaUsageReportState.sourceCount || 0), tone: "neutral" },
                { label: "Placements", value: Number(root.mediaUsageReportState.placementCount || 0), tone: "neutral" },
                { label: "Duplicates", value: Number(root.mediaUsageReportState.duplicateSourceCount || 0), tone: Number(root.mediaUsageReportState.duplicateSourceCount || 0) > 0 ? "warning" : "neutral" },
                { label: "Offline", value: Number(root.mediaUsageReportState.offlineSourceCount || 0), tone: Number(root.mediaUsageReportState.offlineSourceCount || 0) > 0 ? "danger" : "neutral" },
                { label: "Unused", value: Number(root.mediaUsageReportState.unusedSelectionCount || 0), tone: Number(root.mediaUsageReportState.unusedSelectionCount || 0) > 0 ? "warning" : "neutral" }
            ]

            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                Layout.preferredHeight: 48
                radius: theme.radius6
                color: theme.alpha(theme.ink850, 0.82)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.35)

                Column {
                    anchors.centerIn: parent
                    spacing: 1
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: String(modelData.value)
                        color: modelData.tone === "danger" ? theme.error
                              : modelData.tone === "warning" ? theme.warning
                              : theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }
                    Label {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: modelData.label
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 9
                    }
                }
            }
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 7

            Label {
                Layout.fillWidth: true
                visible: Number(root.mediaUsageReportState.sourceCount || 0) === 0
                text: "No media sources are placed in this document."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }

            Repeater {
                model: root.mediaUsageReportState.entries || []

                delegate: Rectangle {
                    id: mediaUsageCard
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: mediaUsageCardContent.implicitHeight + 16
                    radius: theme.radius8
                    color: theme.alpha(theme.ink850, 0.82)
                    border.width: 1
                    border.color: Boolean(modelData.offline)
                                  ? theme.alpha(theme.error, 0.75)
                                  : theme.alpha(theme.lineSoft, 0.4)

                    ColumnLayout {
                        id: mediaUsageCardContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: String(mediaUsageCard.modelData.displayLabel || "Media")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }
                            StatusChip {
                                compact: true
                                visible: Boolean(mediaUsageCard.modelData.duplicate)
                                tone: "warning"
                                text: Number(mediaUsageCard.modelData.placementCount || 0) + " placements"
                            }
                            StatusChip {
                                compact: true
                                visible: Boolean(mediaUsageCard.modelData.hasReusedCrop)
                                tone: "neutral"
                                text: Number(mediaUsageCard.modelData.reusedCropCount || 0) + " reused crop"
                            }
                            StatusChip {
                                compact: true
                                visible: Boolean(mediaUsageCard.modelData.offline)
                                tone: "error"
                                text: "Offline"
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: String(mediaUsageCard.modelData.sourcePath || "")
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            text: "Boards " + String(mediaUsageCard.modelData.boardNumbersLabel || "—") +
                                  " · " + Number(mediaUsageCard.modelData.cropVariantCount || 0) +
                                  " crop variant" +
                                  (Number(mediaUsageCard.modelData.cropVariantCount || 0) === 1 ? "" : "s")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            elide: Text.ElideNone
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 4

                            Repeater {
                                model: mediaUsageCard.modelData.placements || []

                                delegate: ActionButton {
                                    required property var modelData
                                    compact: true
                                    text: "Board " + Number(modelData.boardNumber || 1) +
                                          (String(modelData.slot || "").length > 0
                                           ? " · " + String(modelData.slot) : " · layer")
                                    onClicked: {
                                        carouselDocument.focusMediaUsagePlacement(
                                            Number(modelData.boardIndex || 0),
                                            String(modelData.itemId || ""),
                                            String(modelData.slot || "primary"))
                                        carouselMediaUsageDialog.close()
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                visible: Number(root.mediaUsageReportState.unusedSelectionCount || 0) > 0
                text: "Unused autofill selection"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
            }

            Repeater {
                model: root.mediaUsageReportState.unusedSelections || []

                delegate: Rectangle {
                    id: unusedMediaCard
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: unusedMediaRow.implicitHeight + 12
                    radius: theme.radius6
                    color: theme.alpha(theme.warning, 0.08)
                    border.width: 1
                    border.color: theme.alpha(theme.warning, 0.35)

                    RowLayout {
                        id: unusedMediaRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 6
                        spacing: 8
                        Label {
                            Layout.fillWidth: true
                            text: String(unusedMediaCard.modelData.displayLabel || "Media") +
                                  " · " + String(unusedMediaCard.modelData.reason || "Not used")
                            color: theme.textMain
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                        StatusChip {
                            compact: true
                            tone: Boolean(unusedMediaCard.modelData.offline) ? "error" : "warning"
                            text: Boolean(unusedMediaCard.modelData.offline) ? "Offline" : "Unused"
                        }
                    }
                }
            }
        }
    }
}
