pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQml
import "../../components"

ColumnLayout {
    id: content
    readonly property int missingResourceCount: Number(content.carouselClipboardPasteDialog.preview.missingAssetCount || 0) + Number(content.carouselClipboardPasteDialog.preview.missingFontCount || 0)
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselClipboardPasteDialog
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function focusInitialTarget() { preserveSourceChip.forceActiveFocus(Qt.TabFocusReason) }

    RowLayout {
        objectName: "carouselClipboardPasteHeader"
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: content.theme.space4

            Label {
                Layout.fillWidth: true
                text: "Paste Carousel content"
                wrapMode: Text.WordWrap
                color: content.theme.textStrong
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeDialogTitle
                font.weight: Font.DemiBold
                elide: Text.ElideNone
            }

            Label {
                Layout.fillWidth: true
                text: String(content.carouselClipboardPasteDialog.preview.sourceDocumentName || "Another Carousel") +
                      " · " + String(content.carouselClipboardPasteDialog.preview.sourceFormat || "custom")
                color: content.theme.textMuted
                font.family: content.theme.fontMono
                font.pixelSize: content.theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }

        ActionButton {
            objectName: "carouselClipboardPasteCloseButton"
            compact: true
            text: "Close"
            iconName: "close"
            tooltipText: "Cancel paste"
            onClicked: content.carouselClipboardPasteDialog.close()
        }
    }

    ColumnLayout {
        objectName: "carouselClipboardPasteSummary"
        Layout.fillWidth: true
        spacing: content.theme.space6
        Label {
            Layout.fillWidth: true
            text: (String(content.carouselClipboardPasteDialog.preview.kind || "components") === "boards"
                   ? Number(content.carouselClipboardPasteDialog.preview.boardCount || 0) + (Number(content.carouselClipboardPasteDialog.preview.boardCount || 0) === 1 ? " board · " : " boards · ") : "")
                  + Number(content.carouselClipboardPasteDialog.preview.itemCount || 0) + (Number(content.carouselClipboardPasteDialog.preview.itemCount || 0) === 1 ? " layer" : " layers")
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeControl
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "carouselClipboardResourceSummary"
            Layout.fillWidth: true
            text: content.missingResourceCount > 0
                  ? Number(content.carouselClipboardPasteDialog.preview.missingAssetCount || 0) + (Number(content.carouselClipboardPasteDialog.preview.missingAssetCount || 0) === 1 ? " missing asset · " : " missing assets · ")
                    + Number(content.carouselClipboardPasteDialog.preview.missingFontCount || 0) + (Number(content.carouselClipboardPasteDialog.preview.missingFontCount || 0) === 1 ? " missing font" : " missing fonts")
                  : "All assets and fonts available"
            color: content.missingResourceCount > 0 ? content.theme.warning : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WordWrap
        }
    }

    ScrollView {
        id: pasteScroll
        objectName: "carouselClipboardPasteSettings"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(content.theme.scaled(300), pasteBody.implicitHeight)
        Layout.minimumHeight: content.theme.scaled(80)
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            id: pasteBody
            width: pasteScroll.availableWidth
            spacing: content.theme.space10

            InspectorSection {
                Layout.fillWidth: true
                title: "Missing resources"
                visible: content.missingResourceCount > 0
                expandable: true
                expanded: true

                Repeater {
                    model: content.carouselClipboardPasteDialog.preview.missingAssets || []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: "Missing asset · " + String(modelData.name || modelData.path || "Unknown file")
                        color: content.theme.warning
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                }

                Repeater {
                    model: content.carouselClipboardPasteDialog.preview.missingFonts || []
                    delegate: Label {
                        required property var modelData
                        Layout.fillWidth: true
                        text: "Missing font · " + String(modelData.family || "Unknown face")
                        color: content.theme.warning
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        elide: Text.ElideNone
                    }
                }

                Label {
                    Layout.fillWidth: true
                    visible: Number(content.carouselClipboardPasteDialog.preview.missingAssetCount || 0) +
                             Number(content.carouselClipboardPasteDialog.preview.missingFontCount || 0) > 0
                    text: "Missing resources can be relinked after paste."
                    color: content.theme.textFaint
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }

            InspectorSection {
                objectName: "carouselClipboardStyleMappingSection"
                Layout.fillWidth: true
                title: "Destination styling"
                expandable: false

                Flow {
                    Layout.fillWidth: true
                    Layout.preferredHeight: childrenRect.height
                    spacing: content.theme.space6

                    FilterChip {
                        id: preserveSourceChip
                        objectName: "carouselClipboardPreserveSourceButton"
                        text: "Preserve source"
                        active: content.root.clipboardPasteStylePolicy === "preserve"
                        onClicked: content.root.clipboardPasteStylePolicy = "preserve"
                    }

                    FilterChip {
                        text: "Map brand roles"
                        active: content.root.clipboardPasteStylePolicy === "map-brand"
                        enabled: Boolean(content.carouselClipboardPasteDialog.preview.canMapBrand)
                        onClicked: content.root.clipboardPasteStylePolicy = "map-brand"
                    }
                }

                ToneComboBox {
                    objectName: "carouselClipboardBrandTargetCombo"
                    Layout.fillWidth: true
                    visible: content.root.clipboardPasteStylePolicy === "map-brand"
                    model: content.carouselClipboardPasteDialog.preview.brandTargets || []
                    textRole: "name"
                    valueRole: "brandId"
                    onCurrentValueChanged: content.root.clipboardPasteBrandId = String(currentValue || "")
                }

                Label {
                    Layout.fillWidth: true
                    text: content.root.clipboardPasteStylePolicy === "map-brand"
                          ? "Map text roles, accent shapes, brush colors, and pasted board backgrounds to the selected destination kit."
                          : "Keep the source typography, colors, and board backgrounds unchanged."
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }
        }
    }

    RowLayout {
        objectName: "carouselClipboardPasteFooter"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Item { Layout.fillWidth: true }

        ActionButton {
            objectName: "carouselClipboardPasteCancelButton"
            compact: true
            text: "Cancel"
            onClicked: content.carouselClipboardPasteDialog.close()
        }

        ActionButton {
            objectName: "carouselApplyClipboardPasteButton"
            compact: true
            tone: "primary"
            text: String(content.carouselClipboardPasteDialog.preview.kind || "components") === "boards"
                  ? "Paste boards"
                  : "Paste layers"
            enabled: Boolean(content.carouselClipboardPasteDialog.preview.canApply) &&
                     (content.root.clipboardPasteStylePolicy !== "map-brand" ||
                      content.root.clipboardPasteBrandId.length > 0)
            disabledReason: Boolean(content.carouselClipboardPasteDialog.preview.enoughBoardCapacity)
                            ? "Choose a destination brand kit."
                            : "This paste would exceed the 20-board limit."
            onClicked: {
                if (content.carouselDocument.applyClipboardPaste(
                            content.root.clipboardPasteStylePolicy,
                            content.root.clipboardPasteBrandId))
                    content.carouselClipboardPasteDialog.close()
            }
        }
    }
}
