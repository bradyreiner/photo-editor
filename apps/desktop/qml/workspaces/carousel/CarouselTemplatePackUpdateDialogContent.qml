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
    required property var carouselTemplatePackUpdateDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 10

    Label {
        Layout.fillWidth: true
        text: String(carouselDocument.templatePackUpdatePreview.templateName || "Template pack")
              + " · v" + Number(carouselDocument.templatePackUpdatePreview.fromVersion || 0)
              + " → v" + Number(carouselDocument.templatePackUpdatePreview.toVersion || 0)
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 21
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: "This review is non-mutating. Only unchanged v1 components are eligible; local overrides and missing components are never recreated or overwritten."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    Flow {
        Layout.fillWidth: true
        spacing: 5
        StatusChip {
            text: Number(carouselDocument.templatePackUpdatePreview.updateCount || 0) + " updates"
            compact: true
        }
        StatusChip {
            text: Number(carouselDocument.templatePackUpdatePreview.preservedOverrideCount || 0)
                  + " local overrides preserved"
            tone: Number(carouselDocument.templatePackUpdatePreview.preservedOverrideCount || 0) > 0
                  ? "warning" : "success"
            compact: true
        }
        StatusChip {
            text: "Boards "
                  + ((carouselDocument.templatePackUpdatePreview.affectedBoardNumbers || []).join(", ") || "metadata only")
            compact: true
        }
    }

    Flickable {
        Layout.fillWidth: true
        Layout.fillHeight: true
        contentWidth: width
        contentHeight: updateChangeColumn.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: updateChangeColumn
            width: parent.width
            spacing: 7

            Repeater {
                model: carouselDocument.templatePackUpdatePreview.changes || []

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 112
                    radius: 8
                    color: theme.alpha(theme.ink850, 0.72)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.35)

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 9
                        spacing: 4
                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.label || "Component change")
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        Label {
                            Layout.fillWidth: true
                            text: String(modelData.summary || "")
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 10
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                        Label {
                            Layout.fillWidth: true
                            text: Number(modelData.updateCount || 0) + " update · "
                                  + Number(modelData.preservedOverrideCount || 0) + " preserved · "
                                  + Number(modelData.missingCount || 0) + " missing — "
                                  + String(modelData.migrationPolicy || "")
                            color: theme.textFaint
                            font.family: theme.fontMono
                            font.pixelSize: 9
                            wrapMode: Text.WordWrap
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 48
        radius: 7
        color: theme.alpha(theme.warning, 0.10)
        border.width: 1
        border.color: theme.alpha(theme.warning, 0.42)
        Label {
            anchors.fill: parent
            anchors.margins: 8
            text: "No automatic migration. Apply approved update creates one undo step and records the migration in this document."
            color: theme.textMain
            font.family: theme.fontUi
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        ActionButton {
            text: "Keep current version"
            compact: true
            onClicked: carouselTemplatePackUpdateDialog.close()
        }
        ActionButton {
            objectName: "carouselApplyTemplatePackUpdateButton"
            text: "Apply approved update"
            tone: "primary"
            compact: true
            enabled: Boolean(carouselDocument.templatePackUpdatePreview.available)
            onClicked: {
                if (carouselDocument.applyPreviewedTemplatePackUpdate())
                    carouselTemplatePackUpdateDialog.close()
            }
        }
    }
}
