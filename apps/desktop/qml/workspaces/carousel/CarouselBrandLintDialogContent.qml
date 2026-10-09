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
    required property var carouselBrandLintDialog
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 18
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: "Brand lint"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            Label {
                text: String(carouselBrandLintDialog.lint.brandName || "Brand") + " · " +
                      Number(carouselBrandLintDialog.lint.issueCount || 0) + " active issue" +
                      (Number(carouselBrandLintDialog.lint.issueCount || 0) === 1 ? "" : "s") + " · " +
                      Number(carouselBrandLintDialog.lint.ignoredCount || 0) + " ignored"
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }
        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Close brand lint"
            onClicked: carouselBrandLintDialog.close()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.alpha(theme.lineSoft, 0.45)
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.leftMargin: 18
        Layout.rightMargin: 18
        Layout.topMargin: 14
        visible: Number(carouselBrandLintDialog.lint.issueCount || 0) === 0
        implicitHeight: 74
        radius: 8
        color: theme.alpha(theme.success, 0.09)
        border.width: 1
        border.color: theme.alpha(theme.success, 0.4)
        ColumnLayout {
            anchors.centerIn: parent
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "No active brand exceptions"
                color: theme.success
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            Label {
                Layout.alignment: Qt.AlignHCenter
                text: "Lint updates whenever the document styling changes."
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
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
            spacing: 8
            Item { Layout.preferredHeight: 8 }
            Repeater {
                model: carouselBrandLintDialog.lint.issues || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    implicitHeight: lintIssueContent.implicitHeight + 18
                    radius: 8
                    color: theme.alpha(theme.ink850, 0.76)
                    border.width: 1
                    border.color: String(modelData.severity || "info") === "warning"
                                  ? theme.alpha(theme.warning, 0.45)
                                  : theme.alpha(theme.lineSoft, 0.34)

                    ColumnLayout {
                        id: lintIssueContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 9
                        spacing: 5
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: String(modelData.code || "brand-issue").split("-").join(" ").toUpperCase() +
                                      (Number(modelData.boardNumber || 0) > 0
                                       ? " · BOARD " + Number(modelData.boardNumber) : "")
                                color: String(modelData.severity || "info") === "warning" ? theme.warning : theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: 9
                                font.weight: Font.Bold
                            }
                            ActionButton {
                                compact: true
                                text: "Select"
                                enabled: Boolean(modelData.canFocus)
                                disabledReason: "This document-wide issue has no single layer to select."
                                onClicked: carouselDocument.focusBrandLintIssue(String(modelData.issueId || ""))
                            }
                            ActionButton {
                                compact: true
                                text: "Ignore"
                                tooltipText: "Keep this exact exception ignored in this carousel."
                                onClicked: carouselDocument.ignoreBrandLintIssue(String(modelData.issueId || ""))
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.message || "Brand exception")
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.details || "")
                            visible: text.length > 0
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: 10 }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.alpha(theme.lineSoft, 0.45)
    }
    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 14
        Label {
            Layout.fillWidth: true
            text: "Ignored exceptions are document-specific and persist after save."
            color: theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: 9
        }
        ActionButton {
            compact: true
            text: "Done"
            onClicked: carouselBrandLintDialog.close()
        }
    }
}
