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
    required property var carouselRelinkDialog
    spacing: 0

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 76
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 4

            Label {
                Layout.fillWidth: true
                text: "Relink Missing Media"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 20
                font.weight: Font.DemiBold
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: carouselRelinkDialog.missingEntries.length + " missing instance" +
                      (carouselRelinkDialog.missingEntries.length === 1 ? "" : "s") +
                      ". Only unique metadata matches can be applied."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ColumnLayout {
            width: Math.max(0, carouselRelinkDialog.width - 32)
            spacing: 8

            Repeater {
                model: carouselRelinkDialog.plan.length > 0
                       ? carouselRelinkDialog.plan
                       : carouselRelinkDialog.missingEntries

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: relinkEntryContent.implicitHeight + 18
                    radius: theme.radius6
                    color: theme.alpha(theme.ink850, 0.76)
                    border.width: 1
                    border.color: Boolean(modelData.safeToApply)
                                  ? theme.alpha(theme.success, 0.5)
                                  : modelData.matchState === "ambiguous" ||
                                    modelData.matchState === "metadata-mismatch"
                                    ? theme.alpha(theme.warning, 0.52)
                                    : theme.alpha(theme.lineSoft, 0.34)

                    ColumnLayout {
                        id: relinkEntryContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 9
                        spacing: 4

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: "Board " + (Number(modelData.boardIndex || 0) + 1) +
                                      "  •  " + String(modelData.displayLabel || "Missing media")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                elide: Text.ElideNone
                            }

                            StatusChip {
                                compact: true
                                text: Boolean(modelData.safeToApply) ? "Verified"
                                      : modelData.matchState === "ambiguous" ? "Ambiguous"
                                      : modelData.matchState === "metadata-mismatch" ? "Mismatch"
                                      : "Missing"
                                tone: Boolean(modelData.safeToApply) ? "success"
                                      : modelData.matchState === "not-found" ? "error" : "warning"
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.originalPath || "")
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: String(modelData.candidatePath || "").length > 0
                            text: "Candidate  " + String(modelData.candidatePath || "")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: String(modelData.matchReason || "").length > 0
                            text: String(modelData.matchReason || "") +
                                  "  Source " + Number(modelData.sourceWidth || 0) + "×" + Number(modelData.sourceHeight || 0) +
                                  ", " + Number(modelData.sourceFileSize || 0) + " bytes."
                            color: Boolean(modelData.safeToApply) ? theme.success : theme.warning
                            font.family: theme.fontUi
                            font.pixelSize: 9
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 66
        color: theme.alpha(theme.ink850, 0.72)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.32)

        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            ActionButton {
                text: "Cancel"
                compact: true
                onClicked: carouselRelinkDialog.close()
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                text: "Scan Folder"
                compact: true
                onClicked: carouselDocument.chooseRelinkDirectory()
            }

            ActionButton {
                text: "Relink " + carouselRelinkDialog.verifiedCount + " Verified"
                compact: true
                tone: "primary"
                enabled: carouselRelinkDialog.verifiedCount > 0
                disabledReason: "Scan a folder and resolve ambiguous or mismatched candidates first."
                onClicked: {
                    if (carouselDocument.applyRelinkPlan())
                        carouselRelinkDialog.close()
                }
            }
        }
    }
}
