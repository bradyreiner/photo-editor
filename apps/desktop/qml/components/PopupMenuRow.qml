import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Button {
    id: root

    required property int index
    required property var modelData
    property var previousItem: ({})
    property string objectNamePrefix: ""
    property color accent: theme.focusRing
    property int labelMaxLines: 2
    property int tooltipTimeout: 3200
    readonly property string itemKey: String(modelData.key || modelData.action || modelData.label || "")
    readonly property string itemLabel: String(modelData.label || itemKey)
    readonly property string itemIcon: String(modelData.icon || "")
    readonly property string itemShortcut: String(modelData.shortcut || modelData.shortcutHint || "")
    readonly property string itemDescription: String(modelData.description || modelData.detail || modelData.summary || "")
    readonly property string disabledFallbackReason: "Unavailable in the current state."
    readonly property string itemDisabledReason: modelData.enabled === false
                                                ? String(modelData.disabledReason || modelData.reason || disabledFallbackReason)
                                                : ""
    readonly property bool itemChecked: modelData.checked === true
    readonly property bool itemDanger: modelData.danger === true || modelData.destructive === true
    readonly property bool previousDanger: previousItem && (previousItem.danger === true || previousItem.destructive === true)
    readonly property bool itemSeparatorBefore: modelData.separatorBefore === true || (itemDanger && index > 0 && !previousDanger)
    readonly property string itemAccessibleDetails: [itemDescription, itemDisabledReason]
                                                    .filter(function(detail) { return detail.length > 0 })
                                                    .join(" ")
    readonly property string itemTooltip: {
        const details = [String(modelData.tooltip || ""), itemDisabledReason, itemDescription]
            .filter(function(detail) { return detail.length > 0 })
        return details.length > 0 ? details.join("\n") : itemLabel
    }
    readonly property bool transientPopupKeyboardScope: true
    readonly property int rowMinimumHeight: theme.controlHeightDense
    readonly property int rowPreferredHeight: theme.menuItemHeight
    readonly property int menuIconColumnWidth: theme.scaled(16)
    readonly property int menuShortcutColumnWidth: theme.scaled(72)
    readonly property string popupMenuRowContract: "popup-menu-row:v4|shared-delegate:true|keyboard:true|hover:true|description:tooltip-accessible|disabled-reason:tooltip-accessible|disabled-fallback:true|checked-state:true|danger-state:true|shortcut-font:ui|readable-text:true|no-elide:true|geometry:true|icon-column:true|shortcut-column:true|role:menu-item"
    readonly property string readableTextContract: "readable-text:v3|ellipsis:none|label-wrap:true|shortcut-wrap:true|tooltip-full:true|accessibility-full:true|details:not-inline|full-label-available:true"
    readonly property string menuGeometryContract: "popup-menu-row-geometry:v2|min-height:dense-token|preferred-height:menu-token|icon-column:16|shortcut-column:72|shortcut-align:right|label-lines:" +
                                                   String(root.labelMaxLines)

    signal activated(string key)
    signal focusIndexRequested(int index)
    signal navigateRequested(int direction)
    signal firstRequested()
    signal lastRequested()
    signal dismissRequested()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: Math.max(root.rowMinimumHeight, root.rowPreferredHeight, menuItemContent.implicitHeight + theme.space8)
    enabled: modelData.enabled !== false
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    objectName: root.objectNamePrefix.length > 0
                ? root.objectNamePrefix + "Item" + index
                : ""
    Accessible.name: itemLabel
    Accessible.description: itemAccessibleDetails
    Accessible.role: Accessible.MenuItem
    Accessible.checked: itemChecked
    AppToolTip {
        targetItem: root
        visible: root.hovered && root.itemTooltip.length > 0
        delay: 350
        timeout: root.tooltipTimeout
        text: root.itemTooltip
    }
    onActiveFocusChanged: if (activeFocus) focusIndexRequested(index)
    onClicked: activated(itemKey)

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Escape) {
            root.dismissRequested()
            event.accepted = true
        } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            event.accepted = true
            if (root.enabled && !event.isAutoRepeat)
                root.activated(root.itemKey)
        } else if (event.key === Qt.Key_Down) {
            navigateRequested(1)
            event.accepted = true
        } else if (event.key === Qt.Key_Up) {
            navigateRequested(-1)
            event.accepted = true
        } else if (event.key === Qt.Key_Home) {
            firstRequested()
            event.accepted = true
        } else if (event.key === Qt.Key_End) {
            lastRequested()
            event.accepted = true
        }
    }

    background: CommandButtonSurface {
        tone: root.itemDanger ? "danger" : "secondary"
        cornerRadius: theme.radiusControl
        hovered: root.hovered
        pressed: root.down
        selected: false
        interactiveEnabled: root.enabled
        fillColor: theme.commandFill(root.itemDanger ? "danger" : "secondary", false,
                                     root.hovered, root.down, root.enabled, true)
        focused: root.visualFocus
    }

    contentItem: ColumnLayout {
        id: menuItemContent
        spacing: root.itemSeparatorBefore ? theme.space6 : 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            visible: root.itemSeparatorBefore
            color: theme.alpha(theme.lineSoft, 0.34)
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            AppIcon {
                objectName: "sharedMenuIcon"
                Layout.preferredWidth: root.menuIconColumnWidth
                Layout.preferredHeight: root.menuIconColumnWidth
                name: root.itemIcon.length > 0 ? root.itemIcon : root.itemChecked ? "check" : ""
                color: root.itemDanger ? theme.error : (root.itemChecked ? theme.selectionAccent : !root.enabled ? theme.textFaint : theme.textMuted)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    objectName: "sharedCommandLabel"
                    fontSizeMode: Text.FixedSize
                    text: root.itemLabel
                    color: !root.enabled
                           ? theme.alpha(theme.textMuted, 0.84)
                           : root.itemDanger
                             ? theme.error
                             : theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeControl
                    font.weight: theme.weightControl
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: root.labelMaxLines
                    elide: Text.ElideNone
                    verticalAlignment: Text.AlignVCenter
                    clip: true
                }

            }

            Text {
                objectName: "sharedCommandCaption"
                visible: root.itemShortcut.length > 0
                text: root.itemShortcut
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideNone
                fontSizeMode: Text.FixedSize
                wrapMode: Text.Wrap
                Layout.preferredWidth: root.itemShortcut.length > 0 ? root.menuShortcutColumnWidth : 0
                Layout.minimumWidth: root.itemShortcut.length > 0 ? root.menuShortcutColumnWidth : 0
                Layout.maximumWidth: root.menuShortcutColumnWidth
                clip: true
            }
            AppIcon {
                objectName: "sharedMenuCheck"
                Accessible.ignored: true
                Layout.preferredWidth: root.menuIconColumnWidth
                Layout.preferredHeight: root.menuIconColumnWidth
                visible: root.itemIcon.length > 0 && root.modelData.checked !== undefined
                opacity: root.itemChecked ? 1 : 0
                name: "check"
                color: theme.selectionAccent
            }
        }
    }
}
