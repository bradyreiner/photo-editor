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
    property int railTrackHeight: theme.sliderTrackBase
    property int handleRestSize: theme.sliderThumbRestBase
    property int handleActiveSize: theme.sliderThumbActiveBase
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
    readonly property string valueGeometryContract: "tool-value-slider-geometry:v2|min-width:120|track-height:3|thumb:12-16|layout-shift:false"

    Theme { id: theme }

    implicitWidth: root.minimumReadableWidth
    implicitHeight: theme.controlHeightDense + (root.showCaption ? caption.implicitHeight + theme.space4 : 0)
    hoverEnabled: true
    activeFocusOnTab: true
    topPadding: root.showCaption ? Math.max(root.padding, caption.implicitHeight + theme.space4) : root.padding
    Accessible.name: label.length > 0 ? label : tooltipText
    Accessible.description: !root.enabled && root.disabledReason.length > 0
                            ? root.disabledReason
                            : (valueText.length > 0 ? valueText : tooltipText)

    Item {
        id: caption
        objectName: root.objectName + "Caption"
        x: root.leftPadding
        width: root.availableWidth
        implicitHeight: Math.max(captionLabel.implicitHeight, captionValue.implicitHeight)
        height: implicitHeight
        visible: root.showCaption

        Label {
            id: captionLabel
            anchors.left: parent.left
            anchors.right: captionValue.left
            anchors.rightMargin: theme.space6
            text: root.label
            color: root.enabled ? theme.textMain : theme.textFaint
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }

        Label {
            id: captionValue
            anchors.right: parent.right
            text: root.valueText
            color: root.enabled ? theme.textMain : theme.textFaint
            font.family: theme.fontNumeric
            font.features: theme.numericFontFeatures
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
        height: theme.scaled(10)

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: theme.scaled(root.railTrackHeight)
            radius: height / 2
            color: theme.alpha(theme.polar, root.enabled ? 0.18 : 0.08)
        }

        Rectangle {
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(theme.scaled(2), root.visualPosition * parent.width)
            height: theme.scaled(root.railTrackHeight)
            radius: height / 2
            color: root.enabled && root.accentFillVisible
                   ? root.accent
                   : theme.alpha(theme.polar, root.enabled ? 0.34 : 0.10)
            opacity: root.pressed || root.hovered || root.visualFocus ? 0.95 : 0.72
        }
    }

    handle: Item {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        width: theme.scaled(Math.max(root.handleRestSize, root.handleActiveSize))
        height: width

        Rectangle {
            anchors.centerIn: parent
            width: theme.scaled(root.pressed || root.visualFocus ? root.handleActiveSize : root.handleRestSize)
            height: width
            radius: width / 2
            color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.62)
            border.width: root.visualFocus ? theme.focusBorderWidth : theme.scaled(1)
            border.color: root.visualFocus ? theme.focusRing : theme.alpha(theme.ink950, 0.78)

            Rectangle {
                anchors.centerIn: parent
                width: parent.width + theme.space8
                height: width
                radius: width / 2
                color: theme.alpha(root.accent, root.pressed ? 0.16 : (root.hovered ? 0.08 : 0))
                z: -1
            }
        }
    }
}
