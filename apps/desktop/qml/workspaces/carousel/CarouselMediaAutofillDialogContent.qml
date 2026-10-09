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
    required property var carouselMediaAutofillDialog
    anchors.fill: parent
    anchors.margins: 16
    spacing: 10

    Label {
        Layout.fillWidth: true
        text: "Autofill media sequence"
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 20
        font.weight: Font.DemiBold
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: "Review the exact ordered source-to-slot mapping. Reorder or skip any source before one atomic apply."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    ListView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        spacing: 5
        model: carouselDocument.mediaAutofillPlan

        delegate: Rectangle {
            required property int index
            required property var modelData
            width: ListView.view.width
            height: 46
            radius: theme.radius6
            color: theme.alpha(theme.ink850, 0.8)
            border.width: 1
            border.color: theme.alpha(theme.lineSoft, 0.35)

            RowLayout {
                anchors.fill: parent
                anchors.margins: 7
                spacing: 6

                Label {
                    Layout.fillWidth: true
                    text: Number(modelData.boardIndex) < 0
                          ? String(modelData.displayLabel || "Media") +
                            "  →  Unused · no open Carousel target"
                          : String(modelData.displayLabel || "Media") + "  →  Board " +
                            Number(modelData.boardNumber || 1) + " · " + String(modelData.slot || "primary")
                    color: Boolean(modelData.skipped) ? theme.textFaint : theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }

                ActionButton {
                    compact: true
                    text: "Up"
                    enabled: index > 0
                    onClicked: carouselDocument.moveMediaAutofillEntry(index, index - 1)
                }
                ActionButton {
                    compact: true
                    text: "Down"
                    enabled: index + 1 < carouselDocument.mediaAutofillPlan.length
                    onClicked: carouselDocument.moveMediaAutofillEntry(index, index + 1)
                }
                FilterChip {
                    text: "Skip"
                    enabled: Number(modelData.boardIndex) >= 0
                    active: Boolean(modelData.skipped)
                    onClicked: carouselDocument.setMediaAutofillEntrySkipped(index, !Boolean(modelData.skipped))
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        ActionButton {
            Layout.fillWidth: true
            text: "Cancel"
            onClicked: carouselMediaAutofillDialog.close()
        }
        ActionButton {
            Layout.fillWidth: true
            text: "Apply mapping"
            tone: "primary"
            enabled: carouselDocument.mediaAutofillPlan.length > 0
            onClicked: {
                if (carouselDocument.applyMediaAutofillPlan())
                    carouselMediaAutofillDialog.close()
            }
        }
    }
}
