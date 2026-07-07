import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property string text: ""
    property string iconName: ""
    property string tone: "neutral" // neutral, accent, info, success, warning, error
    property bool compact: true
    property bool busy: false
    property bool pulse: busy
    property bool dotVisible: iconName.length === 0
    property string actionText: ""
    property bool actionEnabled: true
    property string tooltipText: ""
    property string detailText: ""
    property int minimumReadableWidth: 72
    property int maximumReadableWidth: theme.menuMaxWidth
    readonly property string resolvedTooltipText: tooltipText.length > 0
                                                 ? tooltipText
                                                 : (detailText.length > 0
                                                    ? text + " - " + detailText
                                                    : text)
    readonly property bool attentionTone: tone === "error" || tone === "warning"
    readonly property string statusChipContract: "status-chip:v1|one-line:true|tone:" +
                                                root.tone +
                                                "|busy:" +
                                                String(root.busy) +
                                                "|action:" +
                                                String(root.actionText.length > 0) +
                                                "|minimum-readable-width:" +
                                                String(root.minimumReadableWidth) +
                                                "|calm-neutral:true|attention-only-color:true|tooltip-full:true|accessible:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|label-fit:true|tooltip-full:true|accessibility-full:true"
    readonly property string statusSurfaceContract: "status-surface:v1|render-import-export-model-runtime-warning|compact-density:true|status-before-action:true|retry-action-supported:" +
                                                    String(root.actionText.length > 0)

    signal actionTriggered()

    Theme { id: theme }

    readonly property color toneColor: tone === "error" ? theme.error
                                       : tone === "warning" ? theme.warning
                                       : tone === "success" ? theme.success
                                       : tone === "info" ? theme.accentCool400
                                       : tone === "accent" ? theme.accentWarm400
                                       : theme.textMuted
    readonly property color toneFill: tone === "error" ? theme.alpha(theme.error, 0.1)
                                      : tone === "warning" ? theme.alpha(theme.warning, 0.1)
                                      : tone === "success" ? theme.alpha(theme.success, 0.09)
                                      : tone === "info" ? theme.alpha(theme.accentCool400, 0.1)
                                      : tone === "accent" ? theme.alpha(theme.accentWarm400, 0.1)
                                      : theme.alpha(theme.ink950, 0.72)

    implicitWidth: Math.min(root.maximumReadableWidth,
                            Math.max(root.minimumReadableWidth,
                                     chipRow.implicitWidth + (compact ? 18 : 22)))
    implicitHeight: compact ? 30 : 34
    radius: theme.radiusChip
    color: toneFill
    border.width: theme.chromeBorderWidth
    border.color: theme.alpha(toneColor, tone === "neutral" ? 0.24 : 0.46)
    clip: true

    Accessible.name: root.text
    Accessible.description: root.detailText.length > 0 ? root.detailText : root.resolvedTooltipText
    Accessible.role: Accessible.StaticText

    ToolTip.visible: chipMouse.containsMouse &&
                     (root.resolvedTooltipText.length > 0 ||
                      chipLabel.contentWidth > chipLabel.width + 1)
    ToolTip.delay: 350
    ToolTip.timeout: 3000
    ToolTip.text: root.resolvedTooltipText

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    RowLayout {
        id: chipRow
        anchors.fill: parent
        anchors.leftMargin: compact ? 9 : 11
        anchors.rightMargin: compact ? 9 : 11
        spacing: compact ? 7 : 8

        Rectangle {
            id: statusDot
            visible: root.dotVisible
            Layout.preferredWidth: 6
            Layout.preferredHeight: 6
            radius: 3
            color: root.toneColor
            opacity: root.enabled ? (root.attentionTone || root.busy ? 0.94 : 0.62) : 0.46

            SequentialAnimation on opacity {
                running: root.visible && root.pulse
                loops: Animation.Infinite
                NumberAnimation { from: 0.94; to: 0.34; duration: 520; easing.type: Easing.InOutSine }
                NumberAnimation { from: 0.34; to: 0.94; duration: 520; easing.type: Easing.InOutSine }
            }
        }

        AppIcon {
            visible: root.iconName.length > 0
            Layout.preferredWidth: compact ? 12 : 14
            Layout.preferredHeight: compact ? 12 : 14
            name: root.iconName
            color: root.toneColor
            opacity: root.attentionTone || root.busy || root.tone !== "neutral" ? 1.0 : 0.72
        }

        Text {
            id: chipLabel
            Layout.fillWidth: true
            text: root.text
            color: root.tone === "neutral" ? theme.textMain : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: compact ? 10 : 11
            font.weight: root.tone === "neutral" ? Font.Medium : Font.DemiBold
            font.letterSpacing: 0
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
            verticalAlignment: Text.AlignVCenter
            clip: true
        }

        ActionButton {
            visible: root.actionText.length > 0
            text: root.actionText
            enabled: root.actionEnabled
            compact: true
            embedded: true
            accent: root.toneColor
            tooltipText: root.actionText
            onClicked: root.actionTriggered()
        }
    }
}
