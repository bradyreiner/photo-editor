import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property int rowIndex: -1
    property bool alternate: false
    property bool showIndex: rowIndex >= 0
    property bool showPreview: previewSource.length > 0
    property string previewSource: ""
    property string fallbackIconName: "export"
    property string title: "Export item"
    property string subtitle: ""
    property string destination: ""
    property string preset: ""
    property string format: ""
    property string sizeLabel: ""
    property string statusText: "Ready"
    property string statusTone: "neutral"
    property bool busy: false
    property bool showProgress: false
    property real progressValue: 0
    property color progressAccent: theme.accentWarm500
    property string errorText: ""
    property var menuItems: []
    property bool menuVisible: (menuItems || []).length > 0
    property bool menuEnabled: menuVisible
    property string menuText: "More"
    property string menuTooltipText: "Row actions."
    property int menuWidth: 190
    property bool showRemoveButton: false
    property bool removeEnabled: true
    property string removeText: "Remove"
    property string removeHelperText: "Remove this item from the queue"
    property color accent: toneAccent(statusTone)
    readonly property bool actionsNeedAttention: statusTone === "error" ||
                                                statusText === "Failed" ||
                                                statusText === "Queued" ||
                                                statusText === "Running" ||
                                                statusText === "Canceling" ||
                                                busy
    readonly property bool actionsRevealed: actionsNeedAttention ||
                                            rowHover.hovered ||
                                            activeFocus ||
                                            actionZone.activeFocus
    readonly property int actionZoneWidth: (menuVisible || showRemoveButton) ? 172 : 0
    readonly property string exportQueueRowContract: "export-queue-row:v1|preview:" +
                                                     String(showPreview) +
                                                     "|status-chip:true|progress:" +
                                                     String(showProgress) +
                                                     "|actions:" +
                                                     String(menuVisible) +
                                                     "|remove:" +
                                                     String(showRemoveButton) +
                                                     "|readable-text:true|status-surface:true|calm-row:true|destination:" +
                                                     String(destination.length > 0)
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|destination-wrap:true|metadata-fit:true|tooltip-full:true"
    readonly property string statusSurfaceContract: "status-surface:v2|export-queue|preview-status-progress-actions|status-before-actions:true|error-diagnostic:true|retry-menu:true|actions-hover-focus:true|attention-actions-visible:true|fixed-action-zone:true"
    readonly property string queueActionVisibilityContract: "queue-row-actions:v1|visibility:hover-focus-or-attention|fixed-width:172|no-layout-shift:true|keyboard-row-focus:true|remove-preserved:true|menu-preserved:true"

    signal primaryActionRequested()
    signal menuItemTriggered(string key)
    signal removeRequested()

    Theme { id: theme }

    function toneAccent(tone) {
        if (tone === "error")
            return theme.error
        if (tone === "warning")
            return theme.warning
        if (tone === "success")
            return theme.success
        if (tone === "info")
            return theme.accentCool400
        if (tone === "accent")
            return theme.accentWarm400
        return theme.textMuted
    }

    Layout.fillWidth: true
    implicitHeight: Math.max(64, rowContent.implicitHeight + theme.space16)
    radius: theme.radius8
    color: alternate ? theme.alpha(theme.ink850, 0.62) : theme.alpha(theme.ink900, 0.72)
    border.width: 1
    border.color: statusTone === "error"
                  ? theme.alpha(theme.error, 0.42)
                  : statusTone === "success"
                    ? theme.alpha(theme.accentCool500, 0.34)
                    : theme.alpha(theme.glassEdge, 0.22)
    clip: true
    activeFocusOnTab: true
    Accessible.name: root.title + ", " + root.statusText
    Accessible.description: root.errorText.length > 0 ? root.errorText : root.destination
    Accessible.role: Accessible.ListItem

    HoverHandler { id: rowHover }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: statusTone !== "neutral"
        color: root.accent
        opacity: 0.88
    }

    ColumnLayout {
        id: rowContent
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            Label {
                visible: root.showIndex
                Layout.preferredWidth: 18
                text: String(root.rowIndex + 1)
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            Rectangle {
                visible: root.showPreview || root.fallbackIconName.length > 0
                Layout.preferredWidth: 50
                Layout.preferredHeight: 38
                radius: theme.radius4
                color: theme.alpha(theme.ink950, 0.58)
                border.width: 1
                border.color: theme.alpha(theme.polar, 0.10)
                clip: true

                Image {
                    anchors.fill: parent
                    source: root.showPreview ? root.previewSource : ""
                    visible: root.showPreview
                    fillMode: Image.PreserveAspectCrop
                    smooth: true
                }

                AppIcon {
                    anchors.centerIn: parent
                    visible: !root.showPreview && root.fallbackIconName.length > 0
                    name: root.fallbackIconName
                    width: 14
                    height: 14
                    color: theme.textMuted
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    text: root.title
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                    ToolTip.visible: titleHover.hovered && contentWidth > width + 1
                    ToolTip.delay: 350
                    ToolTip.timeout: 3600
                    ToolTip.text: root.title

                    HoverHandler { id: titleHover }
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }
            }

            ColumnLayout {
                visible: root.preset.length > 0 || root.format.length > 0 || root.sizeLabel.length > 0
                Layout.preferredWidth: 150
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    visible: root.preset.length > 0
                    text: root.preset
                    color: theme.textMain
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignRight
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                    elide: Text.ElideNone
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.format.length > 0 || root.sizeLabel.length > 0
                    text: [root.format, root.sizeLabel].filter(function(part) { return part.length > 0 }).join("  ")
                    color: theme.textMuted
                    font.family: theme.fontMono
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignRight
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 8
                    elide: Text.ElideNone
                }
            }

            StatusChip {
                text: root.statusText
                tone: root.statusTone
                busy: root.busy
                detailText: root.errorText
                tooltipText: "Queue status: " + root.statusText
                Layout.preferredWidth: implicitWidth
                Layout.preferredHeight: implicitHeight
            }

            RowLayout {
                id: actionZone
                objectName: "exportQueueRowActionZone"
                readonly property string actionZoneContract: root.queueActionVisibilityContract
                visible: root.actionZoneWidth > 0
                opacity: root.actionsRevealed ? 1.0 : 0.0
                enabled: root.actionsRevealed
                Layout.preferredWidth: root.actionZoneWidth
                Layout.minimumWidth: root.actionZoneWidth
                Layout.maximumWidth: root.actionZoneWidth
                Layout.preferredHeight: Math.max(menuButton.implicitHeight, removeButton.implicitHeight)
                spacing: theme.space6

                Behavior on opacity {
                    NumberAnimation { duration: 120; easing.type: Easing.OutCubic }
                }

                MenuButton {
                    id: menuButton
                    objectName: "exportQueueRowMenuButton"
                    visible: root.menuVisible
                    enabled: root.menuEnabled && root.actionsRevealed
                    text: root.menuText
                    iconName: "menu"
                    compact: true
                    menuItems: root.menuItems
                    menuWidth: root.menuWidth
                    tooltipText: root.menuTooltipText
                    Layout.fillWidth: root.showRemoveButton
                    Layout.preferredWidth: root.showRemoveButton ? 0 : implicitWidth
                    Layout.preferredHeight: implicitHeight
                    onClicked: root.primaryActionRequested()
                    onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
                }

                ConfirmActionButton {
                    id: removeButton
                    objectName: "exportQueueRowRemoveButton"
                    visible: root.showRemoveButton
                    text: root.removeText
                    enabled: root.removeEnabled && root.actionsRevealed
                    helperText: root.removeHelperText
                    onConfirmed: root.removeRequested()
                }
            }
        }

        Label {
            Layout.fillWidth: true
            visible: root.destination.length > 0
            text: root.destination
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: 11
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }

        ProgressStrip {
            Layout.fillWidth: true
            visible: root.showProgress
            progress: Math.max(0.0, Math.min(1.0, root.progressValue))
            accent: root.progressAccent
        }

        Label {
            Layout.fillWidth: true
            visible: root.errorText.length > 0
            text: root.errorText
            color: theme.error
            font.family: theme.fontUi
            font.pixelSize: 11
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 3
            elide: Text.ElideNone
        }
    }
}
