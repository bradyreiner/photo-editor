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
    required property var carouselStatusNoticesDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    RowLayout {
        Layout.fillWidth: true

        Label {
            Layout.fillWidth: true
            text: "Carousel Status Details"
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 20
            font.weight: Font.DemiBold
            elide: Text.ElideNone
        }

        ActionButton {
            text: "Clear All"
            compact: true
            onClicked: carouselDocument.clearStatusNotices()
        }
    }

    Label {
        Layout.fillWidth: true
        text: "Failures stay here until dismissed, even after later operations replace the live status line."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true

        ColumnLayout {
            width: Math.max(0, carouselStatusNoticesDialog.width - 32)
            spacing: 8

            Repeater {
                model: carouselDocument.statusNotices

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: noticeContent.implicitHeight + 18
                    radius: theme.radius6
                    color: theme.alpha(theme.ink850, 0.76)
                    border.width: 1
                    border.color: modelData.severity === "error"
                                  ? theme.alpha(theme.error, 0.5)
                                  : theme.alpha(theme.warning, 0.46)

                    ColumnLayout {
                        id: noticeContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 9
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true

                            StatusChip {
                                text: String(modelData.category || "details")
                                tone: modelData.severity === "error" ? "error" : "warning"
                                compact: true
                                detailText: String(modelData.message || "")
                            }

                            Label {
                                Layout.fillWidth: true
                                text: String(modelData.createdAt || "")
                                color: theme.textFaint
                                font.family: theme.fontMono
                                font.pixelSize: 9
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideNone
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.message || "")
                            color: modelData.severity === "error" ? theme.error : theme.warning
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Item { Layout.fillWidth: true }

                            ActionButton {
                                text: "Dismiss"
                                compact: true
                                onClicked: carouselDocument.dismissStatusNotice(
                                               Number(modelData.noticeId || 0))
                            }

                            ActionButton {
                                text: String(modelData.actionLabel || "Inspect")
                                compact: true
                                tone: "primary"
                                enabled: String(modelData.actionKey || "") !== "inspect"
                                disabledReason: "Use the visible message and affected surface for inspection."
                                onClicked: {
                                    root.handleStatusNoticeAction(String(modelData.actionKey || ""))
                                    carouselStatusNoticesDialog.close()
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    ActionButton {
        Layout.alignment: Qt.AlignRight
        text: "Close"
        compact: true
        onClicked: carouselStatusNoticesDialog.close()
    }
}
