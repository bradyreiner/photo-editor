import QtQuick
import QtQuick.Layouts
import QtQuick.Controls.Basic

Rectangle {
    id: root

    required property string title
    property string detail: ""
    property real progressValue: -1
    property color accent: theme.accentWarm400
    property bool emphasized: false
    property string tone: emphasized ? "accent" : "neutral"
    property string iconName: tone === "error" ? "error" : tone === "warning" ? "warning"
                              : tone === "success" || progressValue >= 1 ? "check"
                              : tone === "info" ? "info" : ""
    readonly property string activityBannerContract: "activity-banner:v1|title-detail-actions-progress|tone:" +
                                                     root.tone +
                                                     "|progress:" +
                                                     String(root.progressValue >= 0) +
                                                     "|calm-surface:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|detail-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string statusSurfaceContract: "status-surface:v1|progress-summary:true|actions:right|compact-but-readable:true"
    default property alias actionData: actionRow.data

    Theme { id: theme }

    radius: theme.radiusPanel
    color: root.emphasized ? theme.alpha(root.accent, 0.08) : theme.ink900
    border.width: theme.hairlineWidth
    border.color: root.emphasized ? theme.alpha(root.accent, 0.3) : theme.alpha(theme.lineSoft, 0.72)

    implicitHeight: contentColumn.implicitHeight + (theme.space10 * 2)
    Accessible.name: root.title
    Accessible.description: root.detail
    Accessible.role: Accessible.StaticText

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: theme.space10
        spacing: theme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            AppIcon {
                visible: root.iconName.length > 0
                name: root.iconName
                color: root.tone === "error" ? theme.error : root.tone === "warning" ? theme.warning
                       : root.iconName === "check" ? theme.success : theme.textMuted
                Layout.preferredWidth: theme.scaled(18)
                Layout.preferredHeight: theme.scaled(18)
                Layout.alignment: Qt.AlignTop
                Accessible.ignored: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeControl
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.detail.length > 0
                    text: root.detail
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }
            }

            RowLayout {
                id: actionRow
                spacing: theme.space6
            }
        }

        ProgressStrip {
            Layout.fillWidth: true
            progress: root.progressValue
            accent: root.accent
        }
    }
}
