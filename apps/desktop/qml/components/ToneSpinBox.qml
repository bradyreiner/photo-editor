import QtQuick
import QtQuick.Controls.Basic

SpinBox {
    id: root

    onActiveFocusChanged: {
        if (!activeFocus && editorInput.editingText)
            root.commitTextEntry()
    }

    property string accessibleLabel: ""
    property string accessibleDescription: ""
    property string valueUnit: ""
    readonly property string resolvedAccessibleName: {
        const explicitName = String(root.accessibleLabel || "").trim()
        if (explicitName.length > 0)
            return explicitName

        const objectLabel = String(root.objectName || "")
            .replace(/(SpinBox|Stepper|Field|Control)$/i, "")
            .replace(/([a-z0-9])([A-Z])/g, "$1 $2")
            .replace(/[_-]+/g, " ")
            .trim()
        if (objectLabel.length > 0)
            return objectLabel.charAt(0).toUpperCase() + objectLabel.slice(1) + " value"

        return qsTr("Numeric value")
    }
    readonly property string resolvedAccessibleDescription: {
        const explicitDescription = String(root.accessibleDescription || "").trim()
        if (explicitDescription.length > 0)
            return explicitDescription
        const suffix = String(root.valueUnit || "").trim()
        return suffix.length > 0 ? String(root.value) + " " + suffix : String(root.value)
    }
    readonly property string toneSpinBoxContract: "tone-spin-box:v1|shared-component:true|accessible-name:true|value-description:true|keyboard:true|focus-visible:true|theme:true|numeric-entry:true"

    function commitTextEntry() {
        if (!editorInput.editingText)
            return false

        const enteredText = String(editorInput.text).trim()
        const hasValidInput = enteredText.length > 0 && editorInput.acceptableInput
        const parsedValue = hasValidInput ? Number(root.valueFromText(enteredText, root.locale)) : NaN
        const hasParsedValue = Number.isFinite(parsedValue)
        if (!hasParsedValue) {
            editorInput.editingText = false
            editorInput.draftText = root.displayText
            return false
        }

        const nextValue = Math.max(root.from, Math.min(root.to, Math.round(parsedValue)))
        const changed = nextValue !== root.value
        // Keep the editor focused while the committed display text refreshes.
        // The draft binding lets the control retain its value binding until a
        // valid edit is accepted.
        if (changed)
            root.value = nextValue
        editorInput.editingText = false
        editorInput.draftText = root.displayText
        if (changed)
            root.valueModified()
        return changed
    }

    Theme { id: theme }

    implicitWidth: theme.scaled(108)
    implicitHeight: theme.controlHeightTall
    // Mono numerals keep typed and stepped values visually aligned.
    font.family: theme.fontMono
    font.pixelSize: theme.typeControl
    font.weight: theme.weightControl
    leftPadding: theme.space8
    rightPadding: theme.scaled(26)
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    activeFocusOnTab: true

    Accessible.name: root.resolvedAccessibleName
    Accessible.description: root.resolvedAccessibleDescription
    Accessible.role: Accessible.SpinBox
    Accessible.onIncreaseAction: root.increase()
    Accessible.onDecreaseAction: root.decrease()

    contentItem: TextInput {
        id: editorInput
        objectName: root.objectName.length > 0 ? root.objectName + "Editor" : ""
        property bool editingText: false
        property string draftText: root.displayText
        text: editingText ? draftText : root.displayText
        font: root.font
        color: root.enabled ? theme.textStrong : theme.textMuted
        selectionColor: theme.alpha(theme.focusRing, 0.84)
        selectedTextColor: theme.ink950
        horizontalAlignment: TextInput.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !root.editable
        validator: root.validator
        inputMethodHints: Qt.ImhFormattedNumbersOnly
        selectByMouse: true
        activeFocusOnTab: true
        onActiveFocusChanged: {
            if (activeFocus && !editingText) {
                draftText = root.displayText
                editingText = true
            } else if (!activeFocus && editingText) {
                root.commitTextEntry()
            }
        }
        onTextEdited: {
            const editedText = text
            draftText = editedText
            editingText = true
        }
        onAccepted: root.commitTextEntry()
        onEditingFinished: root.commitTextEntry()
    }

    background: Rectangle {
        radius: theme.radiusControl
        color: !root.enabled ? theme.alpha(theme.fieldFill, 0.45)
               : root.hovered ? theme.fieldHoverFill : theme.fieldFill
        border.width: root.activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
        border.color: root.activeFocus ? theme.focusRing : theme.controlEdge
    }

    up.indicator: Rectangle {
        x: root.width - width
        y: 0
        width: theme.scaled(24)
        height: Math.ceil(root.height / 2)
        color: !root.enabled ? "transparent"
               : root.hovered ? theme.editorControlHover : "transparent"

        AppIcon {
            anchors.centerIn: parent
            name: "chevron-up"
            width: theme.scaled(10)
            height: width
            color: root.enabled ? theme.textMain : theme.textFaint
        }
    }

    down.indicator: Rectangle {
        x: root.width - width
        y: Math.floor(root.height / 2)
        width: theme.scaled(24)
        height: root.height - y
        color: !root.enabled ? "transparent"
               : root.hovered ? theme.editorControlHover : "transparent"

        AppIcon {
            anchors.centerIn: parent
            name: "chevron-down"
            width: theme.scaled(10)
            height: width
            color: root.enabled ? theme.textMain : theme.textFaint
        }
    }
}
