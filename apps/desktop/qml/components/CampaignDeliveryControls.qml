pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var controller
    property bool showHeading: true
    objectName: "campaignDeliveryFormatsSection"
    enabled: controller !== null && !controller.exportBusy
    spacing: theme.space6
    Theme { id: theme }
    Label {
        visible: root.showHeading
        text: "Delivery formats"
        color: theme.textMain
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        font.weight: Font.DemiBold
    }
    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: theme.space5
        Repeater {
            model: root.controller ? root.controller.exportDeliveryFormats : []
            delegate: FilterChip {
                required property var modelData
                objectName: "campaignDeliveryFormat_" + modelData.id
                text: modelData.name
                active: modelData.selected
                onClicked: {
                    const ids = root.controller.exportDeliveryFormatIds.slice();
                    const index = ids.indexOf(modelData.id);
                    if (index >= 0) ids.splice(index, 1);
                    else ids.push(modelData.id);
                    root.controller.setExportDeliveryFormats(ids);
                }
            }
        }
    }
    Flow {
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        visible: root.controller !== null && root.controller.exportDeliveryFormatIds.some(id => id !== "original")
        spacing: theme.space5
        Repeater {
            model: ["fit", "fill"]
            delegate: FilterChip {
                required property string modelData
                objectName: "campaignDeliveryFraming_" + modelData
                text: modelData === "fit" ? "Fit artwork" : "Fill and crop"
                active: root.controller !== null && root.controller.exportDeliveryFitMode === modelData
                onClicked: root.controller.setExportDeliveryFitMode(modelData)
            }
        }
    }
    Label {
        Layout.fillWidth: true
        text: "Export several sizes in one job. Fit keeps the full artwork; Fill crops from the center. Video keeps its original format."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
}
