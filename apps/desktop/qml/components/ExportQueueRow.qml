import QtQuick
import QtQuick.Controls.Basic
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
    readonly property bool narrowRow: width < theme.scaled(680)
    readonly property int actionZoneWidth: (menuVisible || showRemoveButton) ? (narrowRow ? theme.scaled(showRemoveButton && menuVisible ? 76 : 36) : 172) : 0
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
    readonly property string readableTextContract: "readable-text:v2|ellipsis:destination-basename-only|title-wrap:true|destination:tooltip-full|metadata-fit:true|accessibility-full:true"
    readonly property string statusSurfaceContract: "status-surface:v2|export-queue|preview-status-progress-actions|status-before-actions:true|error-diagnostic:true|retry-menu:true|actions-hover-focus:true|attention-actions-visible:true|fixed-action-zone:true"
    readonly property string queueActionVisibilityContract: "queue-row-actions:v2|visibility:hover-focus-or-attention|reserved-width:responsive|no-layout-shift:true|keyboard-row-focus:true|remove-preserved:true|menu-preserved:true"

    signal primaryActionRequested()
    signal menuItemTriggered(string key)
    signal removeRequested()

    Theme { id: theme }

    property bool completionObserverReady: false
    property string observedStatus: ""
    Component.onCompleted: {
        observedStatus = statusText
        completionObserverReady = true
    }
    onStatusTextChanged: {
        const wasPending = observedStatus === "Queued" || observedStatus === "Running" ||
                           observedStatus === "Saving" || observedStatus === "Paused" ||
                           observedStatus === "Canceling"
        if (completionObserverReady && wasPending && statusText === "Completed")
            deliveryCaustic.complete()
        observedStatus = statusText
    }
    CompletionCaustic {
        id: deliveryCaustic
        objectName: "exportRowCompletionCaustic"
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 3
        width: 104
        height: 10
        duration: 620
        intensity: 0.82
    }

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

    function destinationDisplayName(path) {
        const normalizedPath = String(path || "").replace(/[\\/]+$/, "")
        if (normalizedPath.length === 0)
            return ""
        const parts = normalizedPath.split(/[\\/]/)
        return parts.length > 0 ? parts[parts.length - 1] : normalizedPath
    }

    Layout.fillWidth: true
    implicitHeight: Math.max(theme.scaled(60), rowContent.implicitHeight + theme.space16)
    radius: theme.radiusPanel
    color: rowHover.hovered ? theme.editorControlHover : "transparent"
    border.width: activeFocus ? theme.focusBorderWidth : 0
    border.color: theme.focusRing
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
        visible: statusTone === "error"
        color: root.accent
        opacity: 0.88
    }

    ColumnLayout {
        id: rowContent
        anchors.fill: parent
        anchors.margins: theme.space8
        anchors.bottomMargin: theme.space8
        spacing: theme.space6

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            Label {
                visible: root.showIndex
                Layout.preferredWidth: 18
                text: String(root.rowIndex + 1)
                color: theme.textMuted
                font.family: theme.fontUi
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
                    id: popupTooltipAnchor1
                    Layout.fillWidth: true
                    text: root.title
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                    AppToolTip {
                        id: sharedTooltip2
                        targetItem: popupTooltipAnchor1
                        visible: popupTooltipAnchor1.visible && (titleHover.hovered && popupTooltipAnchor1.contentWidth > popupTooltipAnchor1.width + 1)
                        delay: 350
                        timeout: 3600
                        text: root.title
                    }
                    HoverHandler { id: titleHover }
                }

                Label {
                    Layout.fillWidth: true
                    visible: !root.narrowRow && root.subtitle.length > 0
                    text: root.subtitle
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }
            }

            ColumnLayout {
                visible: !root.narrowRow && (root.preset.length > 0 || root.format.length > 0 || root.sizeLabel.length > 0)
                Layout.preferredWidth: 150
                spacing: 2

                Label {
                    Layout.fillWidth: true
                    visible: root.preset.length > 0
                    text: root.preset
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignRight
                    elide: Text.ElideNone
                }

                Label {
                    Layout.fillWidth: true
                    visible: root.format.length > 0 || root.sizeLabel.length > 0
                    text: [root.format, root.sizeLabel].filter(function(part) { return part.length > 0 }).join("  ")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    horizontalAlignment: Text.AlignRight
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
                    showText: !root.narrowRow
                    showMenuIndicator: !root.narrowRow
                    primaryOpensMenu: root.narrowRow
                    iconName: "more"
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
                    showText: !root.narrowRow
                    iconName: "close"
                    enabled: root.removeEnabled && root.actionsRevealed
                    helperText: root.removeHelperText
                    onConfirmed: root.removeRequested()
                }
            }
        }

        Label {
            id: destinationLabel
            objectName: "exportQueueDestinationLabel"
            Layout.fillWidth: true
            visible: root.destination.length > 0
            text: root.destinationDisplayName(root.destination)
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.NoWrap
            elide: Text.ElideMiddle

            HoverHandler { id: destinationHover }
            AppToolTip {
                targetItem: destinationLabel
                visible: destinationHover.hovered
                text: root.destination
            }
        }

        ProgressStrip {
            Layout.preferredHeight: 2
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
