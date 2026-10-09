import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    property string title: ""
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool canComplete: false
    property string doneText: "Done"
    property string cancelText: "Cancel"
    property string doneTooltip: "Finish the active tool."
    property string cancelTooltip: "Cancel or exit the active tool."
    property string titleObjectName: "toolHudTitleLabel"
    property string subtitleObjectName: "toolHudSubtitleLabel"
    property string doneButtonObjectName: "toolHudDoneButton"
    property string cancelButtonObjectName: "toolHudCancelButton"
    property string activeFamilyLabel: title
    property string activeVariantLabel: subtitle
    property int doneMinimumReadableWidth: 74
    property int cancelMinimumReadableWidth: 88
    readonly property int hudHeight: 44
    readonly property string layoutContract: "tool-hud:v2|stable-height:true|status-title-values-done-cancel:true|flex-status-column:true|fixed-actions:true|subdued-chrome:true|keyboard:enter-escape|readable-text:true|no-elide:true|no-overlap:true"
    readonly property string toolDiscoveryContract: "tool-hud-discovery:v1|active-family:" +
                                                    root.activeFamilyLabel +
                                                    "|active-variant:" +
                                                    root.activeVariantLabel +
                                                    "|one-next-action:true|done-cancel-visible:true|stale-on-switch:false"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|title-fit:true|subtitle-fit:true|tooltip-full:true|accessibility-full:true|min-title-px:9|min-subtitle-px:8"
    readonly property string commandSemanticsContract: "tool-hud-command-semantics:v1|done:exit-tool-state|cancel:abort-pending-or-exit|apply:not-used|close:not-used|enter:done-when-completable|escape:cancel"

    signal doneRequested()
    signal cancelRequested()

    Theme { id: theme }

    implicitWidth: hudContent.implicitWidth + theme.space18
    implicitHeight: root.hudHeight
    radius: 0
    color: "transparent"
    border.width: 0
    border.color: "transparent"
    clip: false
    activeFocusOnTab: true
    Accessible.name: root.title
    Accessible.description: root.subtitle
    Keys.onReturnPressed: if (root.canComplete) root.doneRequested()
    Keys.onEnterPressed: if (root.canComplete) root.doneRequested()
    Keys.onEscapePressed: root.cancelRequested()

    RowLayout {
        id: hudContent
        anchors.fill: parent
        anchors.leftMargin: theme.space10
        anchors.rightMargin: theme.space8
        spacing: theme.space8

        Rectangle {
            Layout.preferredWidth: 8
            Layout.preferredHeight: 8
            radius: 4
            color: root.accent
        }

        ColumnLayout {
            Layout.preferredWidth: 188
            Layout.maximumWidth: 280
            Layout.fillWidth: true
            spacing: 1

            Label {
                objectName: root.titleObjectName
                Layout.fillWidth: true
                text: root.title
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 0
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 9
                maximumLineCount: 1
                elide: Text.ElideNone
                ToolTip.visible: titleHover.hovered && contentWidth > width + 1
                ToolTip.delay: 300
                ToolTip.text: root.title

                HoverHandler { id: titleHover }
            }

            Label {
                objectName: root.subtitleObjectName
                Layout.fillWidth: true
                text: root.subtitle
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 9
                font.letterSpacing: 0
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                maximumLineCount: 1
                elide: Text.ElideNone
                ToolTip.visible: subtitleHover.hovered && contentWidth > width + 1
                ToolTip.delay: 300
                ToolTip.text: root.subtitle

                HoverHandler { id: subtitleHover }
            }
        }

        Components.ActionButton {
            objectName: root.doneButtonObjectName
            minimumReadableWidth: root.doneMinimumReadableWidth
            text: root.doneText
            iconName: "check"
            tone: "primary"
            accent: root.accent
            compact: true
            enabled: root.canComplete
            tooltipText: root.doneTooltip
            onClicked: root.doneRequested()
        }

        Components.ActionButton {
            objectName: root.cancelButtonObjectName
            minimumReadableWidth: root.cancelMinimumReadableWidth
            text: root.cancelText
            iconName: "close"
            tone: "secondary"
            compact: true
            tooltipText: root.cancelTooltip
            onClicked: root.cancelRequested()
        }
    }
}
