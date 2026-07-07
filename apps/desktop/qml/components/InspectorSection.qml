import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "." as Components

Rectangle {
    id: root

    required property string title
    property string subtitle: ""
    property color accent: theme.accentWarm400
    property bool showAccentMarker: true
    property bool uppercaseTitle: true
    property string titleFontFamily: theme.fontMono
    property int titlePixelSize: 10
    property int titleWeight: Font.DemiBold
    property real titleLetterSpacing: 0
    property string disabledReason: ""
    property bool changed: false
    property bool beforeAfterAvailable: false
    property bool beforeAfterActive: false
    property string beforeAfterObjectName: ""
    property string beforeAfterTooltip: beforeAfterActive ? "Restore this section in before/after comparison." : "Compare before and after for this section only."
    property bool resetAvailable: false
    property string resetObjectName: ""
    property string resetTooltip: "Reset this section."
    property var menuActions: []
    property string menuObjectName: ""
    property string menuTooltip: "More options for " + title
    property bool expandable: false
    property bool expanded: true
    readonly property int headerMinimumHeight: 30
    readonly property int headerActionSize: theme.controlHeightDense
    readonly property int headerChevronColumnWidth: 14
    readonly property int headerAccentColumnWidth: 10
    readonly property bool sectionInteractive: root.enabled && root.expandable
    readonly property string resolvedTitle: root.uppercaseTitle ? root.title.toUpperCase() : root.title
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0
                                                 ? root.title + " - " + root.disabledReason
                                                 : root.title
    default property alias contentData: body.data
    readonly property string sectionContract: "inspector-section:v2|header:title-changed-before-after-reset-menu-chevron|fixed-header:true|fixed-actions:true|icon-first-actions:true|collapse:true|keyboard:true|disabled-reason:true|changed-state:true|no-nested-card:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-fit:true|subtitle-wrap:true|tooltip-full:true|accessibility-full:true"
    readonly property string headerActionContract: "inspector-header-actions:v1|before-after:icon-only-tooltip|reset:icon-only-tooltip|menu:menu-button|fixed-size:" +
                                                   String(root.headerActionSize) +
                                                   "|no-text-labels:true|accessibility-full:true"
    readonly property string sectionGeometryContract: "inspector-section-geometry:v1|left-edge:shared|header-min:30|action-size:" +
                                                      String(root.headerActionSize) +
                                                      "|accent-column:" +
                                                      String(root.headerAccentColumnWidth) +
                                                      "|chevron-column:" +
                                                      String(root.headerChevronColumnWidth) +
                                                      "|body-spacing:" +
                                                      String(theme.space5)

    signal toggled()
    signal beforeAfterRequested()
    signal resetRequested()
    signal menuItemTriggered(string key)

    Theme { id: theme }

    implicitHeight: sectionLayout.implicitHeight + theme.space12
    radius: 0
    color: "transparent"
    border.width: 0
    opacity: root.enabled ? 1.0 : 0.58
    activeFocusOnTab: root.expandable
    Accessible.name: root.title
    Accessible.description: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason : root.subtitle
    Accessible.role: root.expandable ? Accessible.Button : Accessible.StaticText

    function toggleExpanded() {
        if (!root.sectionInteractive)
            return
        root.expanded = !root.expanded
        root.toggled()
    }

    Keys.onReturnPressed: root.toggleExpanded()
    Keys.onEnterPressed: root.toggleExpanded()
    Keys.onSpacePressed: root.toggleExpanded()

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: theme.alpha(theme.lineSoft, 0.30)
    }

    ColumnLayout {
        id: sectionLayout
        anchors.fill: parent
        anchors.leftMargin: theme.space8
        anchors.rightMargin: theme.space8
        anchors.topMargin: theme.space10
        anchors.bottomMargin: theme.space4
        spacing: theme.space8

        Item {
            Layout.fillWidth: true
            implicitHeight: Math.max(root.headerMinimumHeight, headerLayout.implicitHeight)

            HoverHandler {
                id: headerHover
            }

            MouseArea {
                anchors.fill: parent
                enabled: root.sectionInteractive
                cursorShape: root.sectionInteractive ? Qt.PointingHandCursor : Qt.ArrowCursor
                onClicked: root.toggleExpanded()
            }

            ColumnLayout {
                id: headerLayout
                anchors.fill: parent
                spacing: theme.space4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Rectangle {
                        visible: root.showAccentMarker
                        Layout.preferredWidth: root.showAccentMarker ? root.headerAccentColumnWidth : 0
                        Layout.preferredHeight: 1
                        radius: 1
                        color: root.changed ? root.accent : theme.alpha(theme.polar, 0.58)
                    }

                    Label {
                        id: titleLabel
                        text: root.resolvedTitle
                        color: root.enabled ? theme.textStrong : theme.textFaint
                        font.family: root.titleFontFamily
                        font.pixelSize: root.titlePixelSize
                        font.weight: root.titleWeight
                        font.letterSpacing: root.titleLetterSpacing
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideNone
                        fontSizeMode: Text.HorizontalFit
                        minimumPixelSize: 8
                        maximumLineCount: 1
                        Layout.fillWidth: true

                        ToolTip.visible: headerHover.hovered &&
                                         (titleLabel.contentWidth > titleLabel.width + 1 ||
                                          (!root.enabled && root.disabledReason.length > 0))
                        ToolTip.text: root.resolvedTooltipText
                    }

                    Item {
                        visible: !root.changed
                        Layout.preferredWidth: theme.space8
                    }

                    Rectangle {
                        visible: root.changed
                        Layout.preferredWidth: 6
                        Layout.preferredHeight: 6
                        radius: 3
                        color: root.accent
                        ToolTip.visible: changedHover.hovered
                        ToolTip.delay: 250
                        ToolTip.text: root.title + " has changes"

                        HoverHandler {
                            id: changedHover
                        }
                    }

                    Components.ActionButton {
                        objectName: root.beforeAfterObjectName
                        visible: root.beforeAfterAvailable
                        Layout.preferredWidth: root.headerActionSize
                        Layout.preferredHeight: root.headerActionSize
                        showText: false
                        iconName: "compare"
                        tone: "secondary"
                        compact: true
                        embedded: true
                        active: root.beforeAfterActive
                        enabled: root.enabled
                        accent: root.accent
                        tooltipText: root.beforeAfterTooltip
                        onClicked: root.beforeAfterRequested()
                    }

                    Components.ActionButton {
                        objectName: root.resetObjectName
                        visible: root.resetAvailable
                        Layout.preferredWidth: root.headerActionSize
                        Layout.preferredHeight: root.headerActionSize
                        showText: false
                        iconName: "reset"
                        tone: "secondary"
                        compact: true
                        embedded: true
                        enabled: root.enabled
                        tooltipText: root.resetTooltip
                        onClicked: root.resetRequested()
                    }

                    Components.MenuButton {
                        objectName: root.menuObjectName
                        visible: root.menuActions.length > 0
                        Layout.preferredHeight: theme.controlHeightDense
                        text: "More"
                        iconName: "menu"
                        compact: true
                        enabled: root.enabled
                        menuItems: root.menuActions
                        menuWidth: 220
                        tooltipText: root.menuTooltip
                        onClicked: {
                            if (root.menuActions.length > 0)
                                root.menuItemTriggered("default")
                        }
                        onMenuItemTriggered: function(key) { root.menuItemTriggered(key) }
                    }

                    AppIcon {
                        visible: root.expandable
                        name: "chevron-right"
                        rotation: root.expanded ? 90 : 0
                        color: root.expanded ? theme.textMuted : theme.textFaint
                        Layout.preferredWidth: root.headerChevronColumnWidth
                        Layout.preferredHeight: 10
                    }
                }

                Label {
                    visible: root.subtitle.length > 0
                    text: root.subtitle
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            id: body
            visible: !root.expandable || root.expanded
            Layout.fillWidth: true
            spacing: theme.space5
        }
    }
}
