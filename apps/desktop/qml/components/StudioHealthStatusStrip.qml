import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: healthStrip

    required property var studioTheme
    required property var healthItems
    required property var stateColor
    required property color hairlineStrong

    objectName: "healthStatusStrip"
    width: healthRow.implicitWidth + 18
    height: 34
    radius: 4
    color: studioTheme.alpha(studioTheme.ink950, 0.82)
    border.width: studioTheme.chromeBorderWidth
    border.color: hairlineStrong

    RowLayout {
        id: healthRow
        anchors.centerIn: parent
        spacing: 8

        Repeater {
            model: healthStrip.healthItems

            delegate: RowLayout {
                required property var modelData
                spacing: 4

                HoverHandler { id: itemHover }

                Rectangle {
                    Layout.preferredWidth: 7
                    Layout.preferredHeight: 7
                    radius: 4
                    color: healthStrip.stateColor(modelData.state)
                }

                Label {
                    text: modelData.state === "CPU" ? "CPU preview"
                          : modelData.state === "Warn" ? modelData.label + " limited"
                          : modelData.state === "Busy" ? modelData.label + " working"
                          : modelData.state === "Error" ? modelData.label + " error"
                          : modelData.label
                    color: healthStrip.studioTheme.textMain
                    font.family: healthStrip.studioTheme.fontMono
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                }

                ToolTip.visible: itemHover.hovered
                ToolTip.delay: 350
                ToolTip.timeout: 4200
                ToolTip.text: modelData.label + " " + modelData.state + ": " + modelData.detail
            }
        }
    }
}
