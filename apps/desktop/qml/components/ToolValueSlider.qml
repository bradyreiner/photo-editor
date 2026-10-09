import QtQuick
import QtQuick.Controls.Basic

Slider {
    id: root

    property string label: ""
    property string valueText: ""
    property string tooltipText: ""
    property string disabledReason: ""
    property bool showCaption: false
    property color accent: theme.focusRing
    property bool modified: false
    property int railTrackHeight: 2
    property int handleRestSize: 12
    property int handleActiveSize: 14
    property int minimumReadableWidth: 120
    readonly property bool accentFillVisible: root.pressed || root.hovered || root.visualFocus || root.modified
    readonly property string resolvedTooltipText: !root.enabled && root.disabledReason.length > 0
                                                 ? (root.label.length > 0 ? root.label + " - " + root.disabledReason : root.disabledReason)
                                                 : (root.tooltipText.length > 0
                                                    ? root.tooltipText
                                                    : (root.label.length > 0 && root.valueText.length > 0
                                                       ? root.label + " - " + root.valueText
                                                       : (root.label.length > 0 ? root.label : root.valueText)))
    readonly property string toolValueSliderContract: "tool-value-slider:v1|stable-size:true|keyboard:true|hover:true|tooltip-full:true|accessible-value:true|disabled-reason:true|neutral-rest:true|fixed-geometry:true|no-elide:true|premium-rail:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|tooltip-full:true|accessibility-full:true|value-fit:true"
    readonly property string valueGeometryContract: "tool-value-slider-geometry:v1|min-width:120|track-height:2|thumb:12-14|layout-shift:false"

    Theme { id: theme }

    implicitWidth: root.minimumReadableWidth
    implicitHeight: theme.controlHeightDense
    hoverEnabled: true
    activeFocusOnTab: true
    topPadding: root.showCaption ? Math.max(root.padding, 22) : root.padding
    Accessible.name: label.length > 0 ? label : tooltipText
    Accessible.description: !root.enabled && root.disabledReason.length > 0
                            ? root.disabledReason
                            : (valueText.length > 0 ? valueText : tooltipText)

    Item {
        objectName: root.objectName + "Caption"
        x: root.leftPadding
        width: root.availableWidth
        height: 20
        visible: root.showCaption

        Label {
            anchors.left: parent.left
            anchors.right: captionValue.left
            anchors.rightMargin: 5
            text: root.label
            color: root.enabled ? theme.textMain : theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            fontSizeMode: Text.HorizontalFit
            minimumPixelSize: 8
        }

        Label {
            id: captionValue
            anchors.right: parent.right
            text: root.valueText
            color: root.enabled ? theme.textMain : theme.textFaint
            font.family: theme.fontMono
            font.pixelSize: theme.typeCaption
        }
    }


    AppToolTip {
        targetItem: root
        visible: root.hovered && root.resolvedTooltipText.length > 0
        text: root.resolvedTooltipText
    }

    background: Item {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.availableWidth
        height: 10

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: root.railTrackHeight
            radius: 1
            color: theme.alpha(theme.polar, root.enabled ? 0.18 : 0.08)
        }

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(2, root.visualPosition * parent.width)
            height: root.railTrackHeight
            radius: 1
            color: root.enabled && root.accentFillVisible
                   ? root.accent
                   : theme.alpha(theme.polar, root.enabled ? 0.34 : 0.10)
            opacity: root.pressed || root.hovered || root.visualFocus ? 0.95 : 0.72
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: root.pressed || root.visualFocus ? root.handleActiveSize : root.handleRestSize
        height: width
        radius: width / 2
        color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.62)
        border.width: root.visualFocus ? 2 : 1
        border.color: root.visualFocus
                      ? theme.alpha(theme.focusRing, 0.95)
                      : theme.alpha(theme.ink950, 0.78)

        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 8
            height: width
            radius: width / 2
            color: theme.alpha(root.accent, root.pressed ? 0.20 : (root.hovered ? 0.14 : 0))
            z: -1
        }
    }
}
