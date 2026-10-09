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
    required property var carouselBrandApplicationDialog
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 18
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                text: "Review brand application"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 22
                font.weight: Font.Bold
            }
            Label {
                text: String(carouselBrandApplicationDialog.diff.brandName || "Brand") + " · " +
                      Number(carouselBrandApplicationDialog.diff.boardCount || 0) + " board" +
                      (Number(carouselBrandApplicationDialog.diff.boardCount || 0) === 1 ? "" : "s") + " · " +
                      Number(carouselBrandApplicationDialog.diff.layerCount || 0) + " styled layer" +
                      (Number(carouselBrandApplicationDialog.diff.layerCount || 0) === 1 ? "" : "s")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 11
            }
        }
        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Cancel brand application"
            onClicked: carouselBrandApplicationDialog.close()
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.alpha(theme.lineSoft, 0.45)
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: 18
                spacing: 8
                Label {
                    text: "Local overrides"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.Bold
                }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        Layout.fillWidth: true
                        compact: true
                        tone: String(carouselBrandApplicationDialog.diff.overridePolicy || "retain") === "retain" ? "primary" : "secondary"
                        text: "Retain local values"
                        onClicked: carouselDocument.previewBrandKitApplication(
                                       String(carouselBrandApplicationDialog.diff.brandId || ""),
                                       String(carouselBrandApplicationDialog.diff.scope || "current"), "retain")
                    }
                    ActionButton {
                        Layout.fillWidth: true
                        compact: true
                        tone: String(carouselBrandApplicationDialog.diff.overridePolicy || "retain") === "replace" ? "primary" : "secondary"
                        text: "Replace with brand"
                        onClicked: carouselDocument.previewBrandKitApplication(
                                       String(carouselBrandApplicationDialog.diff.brandId || ""),
                                       String(carouselBrandApplicationDialog.diff.scope || "current"), "replace")
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: String(carouselBrandApplicationDialog.diff.overridePolicy || "retain") === "retain"
                          ? "Board styling changes; existing layer values remain local."
                          : "Every listed local layer value is replaced by its mapped brand role."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                visible: Number(carouselBrandApplicationDialog.diff.missingMappings
                                ? carouselBrandApplicationDialog.diff.missingMappings.length : 0) > 0 ||
                         Number(carouselBrandApplicationDialog.diff.contrastRegressions
                                ? carouselBrandApplicationDialog.diff.contrastRegressions.length : 0) > 0
                implicitHeight: reviewWarnings.implicitHeight + 20
                radius: 8
                color: theme.alpha(theme.warning, 0.09)
                border.width: 1
                border.color: theme.alpha(theme.warning, 0.42)

                ColumnLayout {
                    id: reviewWarnings
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    spacing: 6
                    Label {
                        text: "Review exceptions"
                        color: theme.warning
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }
                    Repeater {
                        model: carouselBrandApplicationDialog.diff.missingMappings || []
                        delegate: ActionButton {
                            required property var modelData
                            Layout.fillWidth: true
                            compact: true
                            text: "Board " + Number(modelData.boardNumber || 0) + " · " + String(modelData.message || "Missing mapping")
                            tooltipText: text
                            onClicked: carouselDocument.selectItem(String(modelData.itemId || ""))
                        }
                    }
                    Repeater {
                        model: carouselBrandApplicationDialog.diff.contrastRegressions || []
                        delegate: ActionButton {
                            required property var modelData
                            Layout.fillWidth: true
                            compact: true
                            text: "Board " + Number(modelData.boardNumber || 0) + " · " + String(modelData.message || "Contrast regression")
                            tooltipText: text
                            onClicked: carouselDocument.selectItem(String(modelData.itemId || ""))
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                text: "Boards"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            Repeater {
                model: carouselBrandApplicationDialog.diff.affectedBoards || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    implicitHeight: 48
                    radius: 7
                    color: theme.alpha(theme.ink850, 0.74)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.32)
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        Label {
                            Layout.fillWidth: true
                            text: "Board " + Number(modelData.boardNumber || 0) +
                                  (String(modelData.boardName || "").length > 0 ? " · " + String(modelData.boardName) : "")
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 10
                        }
                        Label {
                            text: String(modelData.beforeBackground || "") + " → " + String(modelData.afterBackground || "")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
                        }
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                text: "Layer diff"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                font.weight: Font.Bold
            }
            Repeater {
                model: carouselBrandApplicationDialog.diff.affectedLayers || []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.leftMargin: 18
                    Layout.rightMargin: 18
                    implicitHeight: layerDiffContent.implicitHeight + 16
                    radius: 7
                    color: theme.alpha(theme.ink850, 0.74)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.32)
                    ColumnLayout {
                        id: layerDiffContent
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: 8
                        spacing: 4
                        RowLayout {
                            Layout.fillWidth: true
                            Label {
                                Layout.fillWidth: true
                                text: "Board " + Number(modelData.boardNumber || 0) + " · " + String(modelData.label || modelData.kind || "Layer")
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                wrapMode: Text.WordWrap
                                elide: Text.ElideNone
                            }
                            ActionButton {
                                compact: true
                                text: "Select"
                                onClicked: carouselDocument.selectItem(String(modelData.itemId || ""))
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            text: "Before: " + carouselBrandApplicationDialog.styleSummary(modelData.before) +
                                  "\nBrand: " + carouselBrandApplicationDialog.styleSummary(modelData.target) +
                                  "\nResult: " + carouselBrandApplicationDialog.styleSummary(modelData.result) +
                                  " · " + String(modelData.overrideAction || "match")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
            Item { Layout.preferredHeight: 8 }
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
        spacing: 8
        Label {
            Layout.fillWidth: true
            text: Number(carouselBrandApplicationDialog.diff.localOverrideCount || 0) +
                  " local override" +
                  (Number(carouselBrandApplicationDialog.diff.localOverrideCount || 0) === 1 ? "" : "s") +
                  " will be " +
                  (String(carouselBrandApplicationDialog.diff.overridePolicy || "retain") === "retain"
                   ? "retained." : "replaced.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 10
        }
        ActionButton {
            compact: true
            text: "Cancel"
            onClicked: carouselBrandApplicationDialog.close()
        }
        ActionButton {
            compact: true
            tone: "primary"
            text: "Apply Reviewed Changes"
            onClicked: {
                if (carouselDocument.applyPreviewedBrandKit())
                    carouselBrandApplicationDialog.close()
            }
        }
    }
}
