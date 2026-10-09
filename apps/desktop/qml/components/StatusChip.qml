import QtQuick
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
    readonly property string resolvedIconName: iconName.length > 0 ? iconName
                                               : tone === "error" ? "error"
                                               : tone === "warning" ? "warning"
                                               : tone === "success" ? "check"
                                               : tone === "info" ? "info" : ""
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
    readonly property color toneFill: tone === "error" ? theme.alpha(theme.error, 0.075)
                                      : tone === "warning" ? theme.alpha(theme.warning, 0.075)
                                      : tone === "success" ? theme.alpha(theme.success, 0.06)
                                      : tone === "info" ? theme.alpha(theme.accentCool400, 0.07)
                                      : tone === "accent" ? theme.alpha(theme.accentWarm400, 0.07)
                                      : theme.alpha(theme.ink950, 0.72)

    implicitWidth: Math.min(root.maximumReadableWidth,
                            Math.max(root.minimumReadableWidth,
                                     chipRow.implicitWidth + (compact ? 18 : 22)))
    implicitHeight: compact ? theme.controlHeight : theme.controlHeightTall
    radius: theme.radiusControl
    color: tone === "warning" ? theme.warningFill : tone === "error" ? theme.errorFill : toneFill
    border.width: theme.hairlineWidth
    border.color: theme.alpha(tone === "neutral" ? theme.lineSoft : toneColor,
                              tone === "neutral" ? 0.62 : 0.3)
    clip: true

    Accessible.name: root.text
    Accessible.description: root.detailText.length > 0 ? root.detailText : root.resolvedTooltipText
    Accessible.role: Accessible.StaticText

    MouseArea {
        id: chipMouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.NoButton
    }

    RowLayout {
        id: chipRow
        anchors.fill: parent
        anchors.leftMargin: root.compact ? 9 : 11
        anchors.rightMargin: root.compact ? 9 : 11
        spacing: root.compact ? 7 : 8

        Rectangle {
            id: statusDot
            visible: root.dotVisible && !root.busy && root.resolvedIconName.length === 0
            Layout.preferredWidth: theme.scaled(6)
            Layout.preferredHeight: theme.scaled(6)
            radius: width / 2
            color: root.toneColor
            opacity: root.enabled ? (root.attentionTone || root.busy ? 0.94 : 0.62) : 0.46

        }

        OpticalActivityIndicator {
            objectName: root.objectName.length > 0 ? root.objectName + "Activity" : "statusActivity"
            visible: root.busy && root.dotVisible && !root.attentionTone
            Layout.preferredWidth: theme.scaled(14)
            Layout.preferredHeight: theme.scaled(14)
            active: root.pulse
            color: theme.opticalLight
        }

        AppIcon {
            visible: root.resolvedIconName.length > 0 && !root.busy
            Layout.preferredWidth: root.compact ? theme.scaled(12) : theme.scaled(14)
            Layout.preferredHeight: root.compact ? theme.scaled(12) : theme.scaled(14)
            name: root.resolvedIconName
            color: root.toneColor
            opacity: root.attentionTone || root.busy || root.tone !== "neutral" ? 1.0 : 0.72
        }

        Text {
            id: chipLabel
            Layout.fillWidth: true
            text: root.text
            color: root.tone === "neutral" ? theme.textMain : theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: root.compact ? theme.typeLabel : theme.typeBody
            font.weight: root.tone === "neutral" ? Font.Medium : Font.DemiBold
            font.letterSpacing: 0
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 10
            verticalAlignment: Text.AlignVCenter
            clip: true
        }

        ActionButton {
            visible: root.actionText.length > 0
            text: root.actionText
            iconName: /^retry\b/i.test(root.actionText) ? "retry" : ""
            enabled: root.actionEnabled
            compact: true
            embedded: true
            accent: root.toneColor
            tooltipText: root.actionText
            onClicked: root.actionTriggered()
        }
    }

    AppToolTip {
        targetItem: root
        visible: chipMouse.containsMouse &&
                 (root.resolvedTooltipText.length > 0 ||
                  chipLabel.contentWidth > chipLabel.width + 1)
        text: root.resolvedTooltipText
    }
}
