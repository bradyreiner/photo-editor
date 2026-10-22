pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: root

    property color accent: theme.accentWarm400
    property bool fitTextToWidth: true
    property bool showFullTextTooltip: true
    property string accessibleLabel: ""
    property int maximumPopupHeight: theme.scaled(360)

    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleLabel || "").trim()
        if (explicitName.length > 0)
            return explicitName

        const objectLabel = String(root.objectName || "")
            .replace(/(ComboBox|Combo|Picker|Selector|Control)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
        if (objectLabel.length > 0)
            return objectLabel.charAt(0).toUpperCase() + objectLabel.slice(1) + " combo box"

        return String(root.displayText || "Choose an option")
    }
    readonly property real popupSpaceBelow: root.popupOverlay
                                             ? root.popupOverlay.height - root.popupAnchorY
                                               - root.height - theme.space8
                                             : root.maximumPopupHeight
    readonly property real popupSpaceAbove: root.popupOverlay
                                             ? root.popupAnchorY - theme.space8
                                             : root.maximumPopupHeight
    readonly property Item popupOverlay: root.popup && root.popup.parent
                                          ? root.popup.parent
                                          : null
    property real popupAnchorX: 0
    property real popupAnchorY: 0
    readonly property real popupHorizontalMargin: root.popupOverlay
                                                   ? Math.min(theme.space8,
                                                              root.popupOverlay.width / 2)
                                                   : 0
    readonly property real popupMaximumWidth: root.popupOverlay
                                               ? Math.max(1, root.popupOverlay.width
                                                             - root.popupHorizontalMargin * 2)
                                               : theme.menuMaxWidth
    readonly property real resolvedPopupHeight: Math.max(1, Math.min(root.maximumPopupHeight,
                                                                     Math.max(root.popupSpaceAbove,
                                                                              root.popupSpaceBelow)))
    readonly property bool popupOpensAbove: root.popupSpaceBelow < Math.min(dropdownList.contentHeight,
                                                                             root.maximumPopupHeight)
                                            && root.popupSpaceAbove > root.popupSpaceBelow
    readonly property string accessibilityContract: "tone-combo-accessibility:v1|name:explicit-object-or-value|role:combobox|value:description|keyboard:true"
    readonly property string popupGeometryContract: "tone-combo-popup:v2|max-height:" +
                                                    String(root.maximumPopupHeight) +
                                                    "|viewport-clamped:true|horizontal-clamp:true|flip-near-edge:true|selected-marker:true|scrollable:true"

    function updatePopupAnchor() {
        if (!root.popupOverlay)
            return
        const origin = root.mapToItem(root.popupOverlay, 0, 0)
        root.popupAnchorX = origin.x
        root.popupAnchorY = origin.y
    }

    onXChanged: if (root.popup && root.popup.visible) root.updatePopupAnchor()
    onYChanged: if (root.popup && root.popup.visible) root.updatePopupAnchor()

    Theme { id: theme }

    implicitHeight: theme.controlHeight
    implicitWidth: Math.max(theme.menuMinWidth, selectedLabel.implicitWidth + theme.space24 + theme.space16)
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl
    font.weight: theme.weightControl
    hoverEnabled: true
    activeFocusOnTab: true
    Accessible.name: root.resolvedAccessibleName
    Accessible.description: root.displayText
    Accessible.role: Accessible.ComboBox

    AppToolTip {
        id: selectedValueToolTip
        targetItem: root
        followPointer: true
        visible: root.hovered && root.showFullTextTooltip && selectedLabel.contentWidth > selectedLabel.width + 1
        delay: 250
        timeout: 3000
        text: root.displayText
    }

    HoverHandler {
        onPointChanged: {
            if (selectedValueToolTip.parent)
                selectedValueToolTip.pointerPosition = root.mapToItem(selectedValueToolTip.parent,
                                                                       point.position.x, point.position.y)
        }
    }

    delegate: ItemDelegate {
        id: optionDelegate
        required property int index
        required property var modelData
        objectName: root.objectName.length > 0
                    ? root.objectName + "Option" + String(index)
                    : "toneComboBoxOption" + String(index)
        readonly property string optionText: String(root.textAt(index))
        implicitHeight: theme.controlHeightDense
        padding: 0
        topPadding: theme.space2
        bottomPadding: theme.space2
        width: root.popup.width - root.popup.leftPadding - root.popup.rightPadding
        highlighted: root.highlightedIndex === index
        AppToolTip {
            targetItem: optionDelegate
            visible: optionDelegate.hovered && root.showFullTextTooltip && optionLabel.contentWidth > optionLabel.width + 1
            delay: 250
            timeout: 3000
            text: optionDelegate.optionText
        }

        readonly property bool selected: root.currentIndex === index

        contentItem: Text {
            id: optionLabel
            text: optionDelegate.optionText
            color: optionDelegate.highlighted ? theme.textStrong : theme.textMain
            font.family: root.font.family
            font.pixelSize: theme.typeControl
            font.weight: theme.weightControl
            verticalAlignment: Text.AlignVCenter
            maximumLineCount: 1
            elide: Text.ElideNone
            fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
            minimumPixelSize: theme.typeMicro
            clip: true
            leftPadding: theme.space8
            rightPadding: theme.space24
        }

        AppIcon {
            objectName: root.objectName.length > 0
                        ? root.objectName + "SelectedMarker" + String(optionDelegate.index)
                        : "toneComboBoxSelectedMarker" + String(optionDelegate.index)
            anchors.right: parent.right
            anchors.rightMargin: theme.space10
            anchors.verticalCenter: parent.verticalCenter
            width: theme.scaled(14)
            height: width
            visible: optionDelegate.selected
            name: "check"
            color: root.accent
        }

        background: Rectangle {
            radius: theme.radiusControl
            color: optionDelegate.highlighted ? theme.opticalHoverFill
                   : optionDelegate.selected ? theme.opticalSelectedFill
                   : "transparent"
            Behavior on color { ColorAnimation { duration: theme.opticalHoverDuration } }
        }
    }

    indicator: AppIcon {
        name: "chevron-right"
        color: root.enabled ? theme.textFaint : theme.alpha(theme.textMuted, 0.62)
        rotation: 90
        width: theme.scaled(10)
        height: theme.scaled(10)
        anchors.right: parent.right
        anchors.rightMargin: theme.space12
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Text {
        id: selectedLabel
        text: root.displayText
        color: root.enabled ? theme.textStrong : theme.alpha(theme.textMuted, 0.68)
        leftPadding: theme.space8
        rightPadding: theme.space24
        verticalAlignment: Text.AlignVCenter
        font.family: root.font.family
        font.pixelSize: root.font.pixelSize
        maximumLineCount: 1
        elide: Text.ElideNone
        fontSizeMode: root.fitTextToWidth ? Text.HorizontalFit : Text.FixedSize
        minimumPixelSize: theme.typeMicro
        clip: true
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: theme.commandFill("secondary", false, root.hovered, root.down, root.enabled, false)
        border.width: theme.commandBorderWidth(root.visualFocus)
        border.color: root.visualFocus ? theme.focusRing : theme.controlEdge
    }

    popup: AppPopup {
        margins: theme.space8
        objectName: root.objectName.length > 0 ? root.objectName + "Popup" : "toneComboBoxPopup"
        parent: root.Overlay.overlay
        x: root.popupOverlay
           ? Math.max(root.popupHorizontalMargin,
                      Math.min(root.popupAnchorX,
                               root.popupOverlay.width - width
                               - root.popupHorizontalMargin))
           : 0
        y: root.popupOpensAbove
           ? root.popupAnchorY - height - theme.space4
           : root.popupAnchorY + root.height + theme.space4
        width: Math.min(popupAvailableWidth, Math.max(1, Math.min(root.popupMaximumWidth, Math.max(root.width, theme.menuMinWidth))))
        height: Math.min(popupAvailableHeight, Math.max(1, Math.min(dropdownList.contentHeight + padding * 2, root.resolvedPopupHeight)))
        padding: theme.space4
        onAboutToShow: root.updatePopupAnchor()

        contentItem: ListView {
            id: dropdownList
            clip: true
            implicitHeight: contentHeight
            model: root.popup.visible ? root.delegateModel : null
            currentIndex: root.highlightedIndex
        }

        background: PopupSurface {}
    }
}
