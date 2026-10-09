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
    required property var carouselRepeatedElementLinkDialog
    anchors.fill: parent
    anchors.margins: 18
    spacing: 12

    RowLayout {
        Layout.fillWidth: true

        Label {
            Layout.fillWidth: true
            text: "Link repeated element"
            color: theme.textStrong
            font.family: theme.fontDisplay
            font.pixelSize: 20
            font.weight: Font.Bold
        }

        ActionButton {
            compact: true
            showText: false
            iconName: "close"
            tooltipText: "Cancel repeated-element link"
            onClicked: carouselRepeatedElementLinkDialog.close()
        }
    }

    ToneTextField {
        id: carouselRepeatedElementLinkNameField
        objectName: "carouselRepeatedElementLinkNameField"
        Layout.fillWidth: true
        placeholderText: "Element name"
        text: root.repeatedElementLinkName
        onTextEdited: root.repeatedElementLinkName = text
    }

    Label {
        Layout.fillWidth: true
        text: "Boards"
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 10
        font.weight: Font.DemiBold
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 4

        Repeater {
            model: [
                { label: "Selected", value: "selected" },
                { label: "Following", value: "following" },
                { label: "All boards", value: "all" }
            ]

            delegate: FilterChip {
                required property var modelData
                Layout.fillWidth: true
                text: modelData.label
                active: root.repeatedElementLinkScope === modelData.value
                onClicked: root.repeatedElementLinkScope = modelData.value
            }
        }
    }

    Label {
        Layout.fillWidth: true
        text: root.repeatedElementTargetCount() + " target board" +
              (root.repeatedElementTargetCount() === 1 ? "" : "s") +
              " · " + Number(selection.selectedCount || 0) + " linked layer" +
              (Number(selection.selectedCount || 0) === 1 ? "" : "s")
        color: theme.textMuted
        font.family: theme.fontMono
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: "Geometry, content, style, and media stay synchronized. Detach any board for a local override."
        color: theme.textFaint
        font.family: theme.fontUi
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ActionButton {
            Layout.fillWidth: true
            text: "Cancel"
            onClicked: carouselRepeatedElementLinkDialog.close()
        }

        ActionButton {
            objectName: "carouselApplyRepeatedElementLinkButton"
            Layout.fillWidth: true
            text: "Create link"
            tone: "primary"
            enabled: root.repeatedElementLinkName.trim().length > 0 &&
                     root.repeatedElementTargetCount() > 1
            disabledReason: "Name the element and choose at least two target boards."
            onClicked: {
                if (carouselDocument.createRepeatedElementLink(
                            root.repeatedElementLinkName,
                            root.repeatedElementLinkScope))
                    carouselRepeatedElementLinkDialog.close()
            }
        }
    }
}
