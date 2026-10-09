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
    required property var carouselSequenceTemplateDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 12

    Label {
        Layout.fillWidth: true
        text: String(root.sequenceTemplatePreview.name || "Sequence template")
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 22
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: String(root.sequenceTemplatePreview.purpose || "")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6
        StatusChip {
            text: String(root.sequenceTemplatePreview.sourceSection) === "custom"
                  ? Number(root.sequenceTemplatePreview.itemCount || 0) + " editable layers"
                  : Number(root.sequenceTemplatePreview.boardCount || 0) + " boards"
            compact: true
        }
        StatusChip {
            text: String(root.sequenceTemplatePreview.sourceSection) === "custom"
                  ? String(root.sequenceTemplatePreview.entryKind || "board")
                  : Number(root.sequenceTemplatePreview.requiredMedia || 0) + " media slots"
            compact: true
        }
        StatusChip {
            text: String(root.sequenceTemplatePreview.supportedRatios || "")
            compact: true
        }
    }

    GridLayout {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: String(root.sequenceTemplatePreview.sourceSection) !== "custom"
        columns: root.width > 760 ? 4 : 3
        columnSpacing: 8
        rowSpacing: 8

        Repeater {
            model: root.sequenceTemplatePreview.previewRoles || []
            delegate: Rectangle {
                id: fullSequenceBoardPreview
                required property int index
                required property var modelData
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 132
                Layout.maximumHeight: 205
                radius: 7
                color: index % 4 === 0 ? "#d8c8b3"
                     : index % 4 === 1 ? "#243038"
                     : index % 4 === 2 ? "#dce8e5" : "#eee8df"
                border.width: 1
                border.color: theme.alpha("#ffffff", 0.18)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 5
                    Label {
                        text: String(fullSequenceBoardPreview.index + 1).padStart(2, "0")
                        color: fullSequenceBoardPreview.index % 4 === 1 ? "#b9c7ce" : "#5c554d"
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }
                    Item { Layout.fillHeight: true }
                    Label {
                        Layout.fillWidth: true
                        text: String(fullSequenceBoardPreview.modelData)
                        color: fullSequenceBoardPreview.index % 4 === 1 ? "#ffffff" : "#171717"
                        font.family: theme.fontDisplay
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 4
                        radius: 2
                        color: fullSequenceBoardPreview.index % 4 === 1 ? "#8df0b5" : "#8b6548"
                    }
                }
            }
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.fillHeight: true
        visible: String(root.sequenceTemplatePreview.sourceSection) === "custom"
        radius: 8
        color: theme.alpha(theme.ink950, 0.72)
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.35)
        clip: true

        Image {
            anchors.centerIn: parent
            width: Math.min(parent.width - 24, 320)
            height: Math.min(parent.height - 24, 400)
            source: String(root.sequenceTemplatePreview.previewSource || "")
            sourceSize: Qt.size(320, 400)
            fillMode: Image.PreserveAspectFit
            asynchronous: true
            cache: false
        }

        Label {
            anchors.centerIn: parent
            visible: String(root.sequenceTemplatePreview.previewSource || "").length === 0
            text: String(root.sequenceTemplatePreview.entryKind) === "component"
                  ? "Reusable component" : "Saved board"
            color: theme.textMuted
            font.family: theme.fontDisplay
            font.pixelSize: 18
            wrapMode: Text.WordWrap
            elide: Text.ElideNone
        }
    }

    Label {
        Layout.fillWidth: true
        text: String(root.sequenceTemplatePreview.sourceSection) === "custom"
              ? "Applying adds this saved template to the current board. Its layers receive unique identities and remain editable; Undo removes the complete application."
              : "Applying replaces the current board sequence. Every generated text, shape, role, media slot, and linked spread remains editable; Undo restores the prior document."
        color: theme.textFaint
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WordWrap
        elide: Text.ElideNone
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        ActionButton {
            text: "Cancel"
            compact: true
            onClicked: carouselSequenceTemplateDialog.close()
        }
        ActionButton {
            objectName: "carouselApplyTemplateLibraryEntryButton"
            text: String(root.sequenceTemplatePreview.sourceSection) === "custom"
                  ? "Add to current board" : "Apply complete sequence"
            tone: "primary"
            compact: true
            onClicked: {
                if (carouselDocument.applyTemplateLibraryEntry(
                            String(root.sequenceTemplatePreview.templateId || "")))
                    carouselSequenceTemplateDialog.close()
            }
        }
    }
}
