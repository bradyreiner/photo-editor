import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: root

    required property int index
    required property var modelData
    property var previousItem: ({})
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
                                                : String(modelData.disabledReason || modelData.reason || "")
    readonly property bool itemChecked: modelData.checked === true
    readonly property bool itemDanger: modelData.danger === true || modelData.destructive === true
    readonly property bool previousDanger: previousItem && (previousItem.danger === true || previousItem.destructive === true)
    readonly property bool itemSeparatorBefore: modelData.separatorBefore === true || (itemDanger && index > 0 && !previousDanger)
    readonly property string itemTooltip: !enabled && itemDisabledReason.length > 0
                                          ? itemLabel + " - " + itemDisabledReason
                                          : String(modelData.tooltip || itemLabel)
    readonly property int rowMinimumHeight: 32
    readonly property int rowPreferredHeight: theme.menuItemHeight
    readonly property int menuIconColumnWidth: 16
    readonly property int menuShortcutColumnWidth: 72
    readonly property string popupMenuRowContract: "popup-menu-row:v3|shared-delegate:true|keyboard:true|hover:true|description:true|disabled-reason:true|disabled-fallback:true|checked-state:true|danger-state:true|shortcut-fit:true|readable-text:true|no-elide:true|geometry:true|icon-column:true|shortcut-column:true"
    readonly property string readableTextContract: "readable-text:v2|ellipsis:none|label-wrap:true|description-wrap:true|disabled-wrap:true|shortcut-fit:true|tooltip-full:true|accessibility-full:true|full-label-available:true"
    readonly property string menuGeometryContract: "popup-menu-row-geometry:v1|min-height:32|preferred-height:34|icon-column:16|shortcut-column:72|shortcut-align:right|label-lines:" +
                                                   String(root.labelMaxLines)

    signal activated(string key)
    signal focusIndexRequested(int index)
    signal navigateRequested(int direction)
    signal firstRequested()
    signal lastRequested()

    Theme { id: theme }

    Layout.fillWidth: true
    implicitHeight: Math.max(root.rowMinimumHeight, root.rowPreferredHeight, menuItemContent.implicitHeight + theme.space8)
    enabled: modelData.enabled !== false
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: itemLabel
    Accessible.description: itemDisabledReason.length > 0 ? itemDisabledReason : itemDescription
    ToolTip.visible: hovered && itemTooltip.length > 0
    ToolTip.delay: 350
    ToolTip.timeout: tooltipTimeout
    ToolTip.text: itemTooltip
    onActiveFocusChanged: if (activeFocus) focusIndexRequested(index)
    onClicked: activated(itemKey)

    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Down) {
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

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled
               ? "transparent"
               : root.down
                 ? theme.alpha(root.accent, 0.16)
                 : root.hovered
                   ? theme.alpha(theme.glassLift, 0.28)
                   : "transparent"
        border.width: root.visualFocus ? 2 : 0
        border.color: theme.alpha(theme.focusRing, 0.9)
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
                Layout.preferredWidth: root.menuIconColumnWidth
                Layout.preferredHeight: root.menuIconColumnWidth
                name: root.itemChecked ? "check" : (root.itemIcon.length > 0 ? root.itemIcon : "menu")
                color: root.itemDanger ? theme.error : (root.itemChecked ? root.accent : theme.textMuted)
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    Layout.fillWidth: true
                    text: root.itemLabel
                    color: !root.enabled
                           ? theme.alpha(theme.textMuted, 0.56)
                           : root.itemDanger
                             ? theme.error
                             : theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    font.weight: root.itemChecked ? Font.DemiBold : Font.Medium
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: root.labelMaxLines
                    elide: Text.ElideNone
                    verticalAlignment: Text.AlignVCenter
                    clip: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.enabled && root.itemDescription.length > 0
                    text: root.itemDescription
                    color: theme.alpha(theme.textMuted, 0.70)
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                    clip: true
                }

                Text {
                    Layout.fillWidth: true
                    visible: !root.enabled && root.itemDisabledReason.length > 0
                    text: root.itemDisabledReason
                    color: theme.alpha(theme.textMuted, 0.64)
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                    clip: true
                }
            }

            Text {
                visible: root.itemShortcut.length > 0
                text: root.itemShortcut
                color: theme.alpha(theme.textMuted, 0.72)
                font.family: theme.fontMono
                font.pixelSize: 10
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                maximumLineCount: 1
                elide: Text.ElideNone
                fontSizeMode: Text.HorizontalFit
                minimumPixelSize: 8
                Layout.preferredWidth: root.itemShortcut.length > 0 ? root.menuShortcutColumnWidth : 0
                Layout.minimumWidth: root.itemShortcut.length > 0 ? root.menuShortcutColumnWidth : 0
                Layout.maximumWidth: root.menuShortcutColumnWidth
                clip: true
            }
        }
    }
}
