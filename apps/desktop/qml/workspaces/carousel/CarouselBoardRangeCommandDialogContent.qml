pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../../components"

ColumnLayout {
    id: content
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselBoardRangeCommandDialog
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function focusInitialTarget() { rangeOperationCombo.forceActiveFocus(Qt.TabFocusReason) }

    RowLayout {
        objectName: "carouselBoardRangeHeader"
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "Apply to board range"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "carouselBoardRangeCloseButton"
            compact: true
            text: "Close"
            onClicked: content.carouselBoardRangeCommandDialog.close()
        }
    }

    ScrollView {
        id: rangeScroll
        objectName: "carouselBoardRangeSettings"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(content.theme.scaled(460), rangeBody.implicitHeight)
        Layout.minimumHeight: content.theme.scaled(80)
        clip: true
        contentWidth: availableWidth
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

        ColumnLayout {
            id: rangeBody
            width: rangeScroll.availableWidth
            spacing: content.theme.space12

            GridLayout {
                Layout.fillWidth: true
                columns: rangeScroll.availableWidth >= content.theme.scaled(400) ? 2 : 1
                columnSpacing: content.theme.space12
                rowSpacing: content.theme.space12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: content.theme.space8
                    Label {
                        text: "Operation"
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                    }
                    ToneComboBox {
                        id: rangeOperationCombo
                        objectName: "carouselBoardRangeOperationCombo"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        accessibleLabel: "Board range operation"
                        textRole: "label"
                        valueRole: "value"
                        model: [
                            { label: "Background", value: "background" },
                            { label: "Layout", value: "template" },
                            { label: "Brand style", value: "brand" },
                            { label: "Spacing", value: "spacing" },
                            { label: "Text style", value: "text-style" }
                        ]
                        currentIndex: {
                            const options = rangeOperationCombo.model || []
                            for (let index = 0; index < options.length; ++index)
                                if (String(options[index].value) === content.root.boardRangeOperation)
                                    return index
                            return -1
                        }
                        onActivated: content.root.setBoardRangeOperation(String(rangeOperationCombo.currentValue))
                    }

                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignTop
                    spacing: content.theme.space8
                    Label {
                        text: "Value"
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: content.theme.space8
                        Rectangle {
                            visible: content.root.boardRangeOperation === "background" && content.root.boardRangeValue.length > 0
                            Layout.preferredWidth: content.theme.controlHeightStandard
                            Layout.preferredHeight: content.theme.controlHeightStandard
                            radius: content.theme.radiusControl
                            color: visible ? content.root.boardRangeValue : "transparent"
                            border.width: 1
                            border.color: content.theme.lineSoft
                            Accessible.role: Accessible.StaticText
                            Accessible.name: "Background color " + content.root.boardRangeValue
                        }
                        ToneComboBox {
                            id: rangeValueCombo
                            objectName: "carouselBoardRangeValueCombo"
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            accessibleLabel: "Board range value"
                            textRole: "label"
                            valueRole: "value"
                            model: content.root.boardRangeValueOptions(content.root.boardRangeOperation)
                            enabled: rangeValueCombo.count > 0
                            displayText: rangeValueCombo.count > 0 ? rangeValueCombo.currentText
                                         : content.root.boardRangeOperation === "brand" ? "No brand kits available"
                                         : content.root.boardRangeOperation === "text-style" ? "No text styles available"
                                         : "No values available"
                            currentIndex: {
                                const options = rangeValueCombo.model || []
                                for (let index = 0; index < options.length; ++index)
                                    if (String(options[index].value) === content.root.boardRangeValue)
                                        return index
                                return -1
                            }
                            onActivated: {
                                content.root.boardRangeValue = String(rangeValueCombo.currentValue)
                                content.root.refreshBoardRangePreview()
                            }
                        }
                    }

                }
            }

            Label {
                text: "Range"
                color: content.theme.textMuted
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeCaption
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: content.theme.space6
                Repeater {
                    model: [
                        { label: "Current", value: "current" },
                        { label: "Selected", value: "selected" },
                        { label: "Following", value: "following" },
                        { label: "All boards", value: "all" }
                    ]
                    delegate: FilterChip {
                        required property var modelData
                        objectName: "carouselBoardRangeScope_" + modelData.value
                        text: modelData.label
                        active: content.root.boardRangeScope === modelData.value
                        onClicked: {
                            content.root.boardRangeScope = modelData.value
                            content.root.refreshBoardRangePreview()
                        }
                    }
                }
            }


        }
    }

    ColumnLayout {
        objectName: "carouselBoardRangePreview"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Label {
            Layout.fillWidth: true
            text: Number(content.root.boardRangePreview.changedBoardCount || 0) + (Number(content.root.boardRangePreview.changedBoardCount || 0) === 1 ? " board will change" : " boards will change") +
                  (Number(content.root.boardRangePreview.affectedLayerCount || 0) > 0
                   ? " · " + Number(content.root.boardRangePreview.affectedLayerCount || 0) + " layers affected" : "")
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeControl
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Label {
            Layout.fillWidth: true
            text: "Boards · " + ((content.root.boardRangePreview.boardNumbers || []).join(" · "))
            color: content.theme.textMuted
            font.family: content.theme.fontMono
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            elide: Text.ElideNone
        }
        Label {
            Layout.fillWidth: true
            visible: !Boolean(content.root.boardRangePreview.available)
            text: String(content.root.boardRangePreview.failureReason || "Choose an operation and value.")
            color: content.theme.warning
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            elide: Text.ElideNone
        }
    }

    RowLayout {
        objectName: "carouselBoardRangeFooter"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "carouselBoardRangeCancelButton"
            compact: true
            text: "Cancel"
            onClicked: content.carouselBoardRangeCommandDialog.close()
        }
        ActionButton {
            objectName: "carouselApplyBoardRangeCommandButton"
            compact: true
            tone: "primary"
            text: "Apply to " + Number(content.root.boardRangePreview.affectedBoardCount || 0) + (Number(content.root.boardRangePreview.affectedBoardCount || 0) === 1 ? " board" : " boards")
            enabled: Boolean(content.root.boardRangePreview.available)
            disabledReason: String(content.root.boardRangePreview.failureReason || "Choose a range command.")
            onClicked: {
                if (content.carouselDocument.applyBoardRangeCommand(
                            content.root.boardRangeOperation,
                            content.root.boardRangeValue,
                            content.root.boardRangeScope))
                    content.carouselBoardRangeCommandDialog.close()
            }
        }
    }
}
