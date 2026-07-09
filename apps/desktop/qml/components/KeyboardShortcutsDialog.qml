pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "keyboardShortcutsDialog"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 860
    height: 620
    padding: 0
    standardButtons: Dialog.NoButton

    readonly property bool macStyle: Qt.platform.os === "osx"
    readonly property string primaryLabel: macStyle ? "Cmd" : "Ctrl"
    readonly property string redoLabel: macStyle ? "Cmd+Shift+Z" : "Ctrl+Y"
    property string shortcutEditorStatus: ""
    property string activeShortcutPreset: "app-native"
    property string shortcutSearchText: ""
    property var editableShortcuts: [
        { "id": "command-palette", "label": "Command Palette", "sequence": primary("K"), "defaultSequence": primary("K") },
        { "id": "workspace-library", "label": "Go To Library", "sequence": primary("1"), "defaultSequence": primary("1") },
        { "id": "workspace-edit", "label": "Go To Edit", "sequence": primary("2"), "defaultSequence": primary("2") },
        { "id": "tool-adjust", "label": "Adjust Tool", "sequence": primaryAlt("A"), "defaultSequence": primaryAlt("A") },
        { "id": "tool-crop", "label": "Crop Tool", "sequence": primaryAlt("C"), "defaultSequence": primaryAlt("C") },
        { "id": "tool-masks", "label": "Masks Tool", "sequence": primaryAlt("M"), "defaultSequence": primaryAlt("M") },
        { "id": "tool-brush", "label": "Brush Tool", "sequence": primaryAlt("B"), "defaultSequence": primaryAlt("B") },
        { "id": "tool-compare", "label": "Compare Tool", "sequence": primaryAlt("P"), "defaultSequence": primaryAlt("P") },
        { "id": "tool-export", "label": "Export Tool", "sequence": primaryAlt("E"), "defaultSequence": primaryAlt("E") },
        { "id": "queue-export", "label": "Start Export", "sequence": primary("Shift+E"), "defaultSequence": primary("Shift+E") },
        { "id": "toggle-full-edit", "label": "Full-Screen Edit", "sequence": "F", "defaultSequence": "F" }
    ]
    readonly property int editableShortcutCount: editableShortcuts.length
    readonly property string dialogVisibilityContract: "dialog-visibility:v1|keyboard-shortcuts|always:title-search-editable-presets-shortcut-sections-close|contextual:status-conflicts-empty-search-platform-modifier|one-expansion:preset-apply-editable-sequence-reset|hover-focus:preset-buttons-edit-fields-reset-controls|diagnostic:conflict-count-search-results|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v1|keyboard-shortcuts|search:header|editable:top|sections:scrollable|keys:fixed-readable|descriptions:wrap|presets:readable-buttons|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v1|search-shortcuts:header-field|edit-sequence:inline-field|reset-sequence:inline-reset|apply-presets:preset-buttons|conflict-detection:set-and-preset|platform-modifiers:mac-or-control|shortcut-help:section-cards"
    readonly property var shortcutPresetMappings: [
        {
            "id": "app-native",
            "label": "App Native",
            "sequences": {
                "command-palette": primary("K"),
                "workspace-library": primary("1"),
                "workspace-edit": primary("2"),
                "tool-adjust": primaryAlt("A"),
                "tool-crop": primaryAlt("C"),
                "tool-masks": primaryAlt("M"),
                "tool-brush": primaryAlt("B"),
                "tool-compare": primaryAlt("P"),
                "tool-export": primaryAlt("E"),
                "queue-export": primary("Shift+E"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "lightroom",
            "label": "Lightroom",
            "sequences": {
                "command-palette": primary("K"),
                "workspace-library": "G",
                "workspace-edit": "D",
                "tool-adjust": primaryAlt("A"),
                "tool-crop": primaryAlt("C"),
                "tool-masks": primaryAlt("M"),
                "tool-brush": primaryAlt("B"),
                "tool-compare": primaryAlt("P"),
                "tool-export": primaryAlt("E"),
                "queue-export": primary("Shift+E"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "photoshop",
            "label": "Photoshop",
            "sequences": {
                "command-palette": primary("Alt+K"),
                "workspace-library": "F5",
                "workspace-edit": "F6",
                "tool-adjust": primaryAlt("A"),
                "tool-crop": primaryAlt("C"),
                "tool-masks": primaryAlt("M"),
                "tool-brush": primaryAlt("B"),
                "tool-compare": primaryAlt("P"),
                "tool-export": primaryAlt("E"),
                "queue-export": primary("Alt+Shift+S"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "capture-one",
            "label": "Capture One",
            "sequences": {
                "command-palette": primary("K"),
                "workspace-library": primary("B"),
                "workspace-edit": primary("E"),
                "tool-adjust": primaryAlt("A"),
                "tool-crop": primaryAlt("C"),
                "tool-masks": primaryAlt("M"),
                "tool-brush": primaryAlt("B"),
                "tool-compare": primaryAlt("P"),
                "tool-export": primaryAlt("E"),
                "queue-export": primary("D"),
                "toggle-full-edit": "F11"
            }
        }
    ]
    readonly property int shortcutPresetCount: shortcutPresetMappings.length
    readonly property var shortcutSections: [
        {
            title: "Global",
            accent: theme.accentCool400,
            items: [
                { "keys": primary("O"), "description": "Import a folder into the library." },
                { "keys": primary("N"), "description": "Create a new timeline." },
                { "keys": primary("1-5"), "description": "Switch directly between Library, Edit, Carousel, Timeline, and Export." },
                { "keys": primary("A"), "description": "Select all visible items in Library, Import, or Timeline." },
                { "keys": primary("Shift+A"), "description": "Clear the active library, import, or timeline selection." },
                { "keys": primary("Shift+E"), "description": "Queue the current selection for export." },
                { "keys": primaryAlt("E"), "description": "Open the export workflow." },
                { "keys": primary(","), "description": "Open preferences." }
            ]
        },
        {
            title: "Edit",
            accent: theme.accentWarm400,
            items: [
                { "keys": primary("Z"), "description": "Undo the last photo adjustment." },
                { "keys": redoLabel, "description": "Redo the most recently undone adjustment." },
                { "keys": primaryAlt("A"), "description": "Open the adjust tool." },
                { "keys": primaryAlt("C"), "description": "Open crop editing." },
                { "keys": primaryAlt("P"), "description": "Open compare view." },
                { "keys": primary("D"), "description": "Deselect the active mask and clear transient selection tools." },
                { "keys": "Esc", "description": "Exit the current mask tool or deselect the active mask." },
                { "keys": "Left", "description": "Move to the previous photo in the filmstrip." },
                { "keys": "Right", "description": "Move to the next photo in the filmstrip." }
            ]
        },
        {
            title: "Masks",
            accent: theme.accentWarm300,
            items: [
                { "keys": "Ctrl+Shift+M", "description": "Create a new selection and linked layer." },
                { "keys": "Ctrl+Shift+A / S / I", "description": "Set the next selection operation to Add, Subtract, or Intersect." },
                { "keys": "Ctrl+Shift+V", "description": "Invert the active mask." },
                { "keys": "Ctrl+Shift+O", "description": "Show or hide the active mask color overlay." },
                { "keys": "Ctrl+Shift+U", "description": "Toggle the solid mask outline." },
                { "keys": "Ctrl+Shift+L / K", "description": "Switch to the next active layer target or active mask target." },
                { "keys": "Ctrl+Alt+Shift+L / K", "description": "Switch to the previous active layer target or active mask target." },
                { "keys": "Ctrl+Shift+B", "description": "Return adjustments to the Base Image target." },
                { "keys": "B", "description": "Start the brush-add tool on the active mask." },
                { "keys": "E", "description": "Start the brush-erase tool on the active mask." },
                { "keys": "X (hold)", "description": "Temporarily erase with the brush, then restore the previous brush operation." },
                { "keys": "[ / ]", "description": "Decrease / increase brush size." },
                { "keys": "Shift+[ / Shift+]", "description": "Decrease / increase brush hardness." },
                { "keys": "Alt+[ / Alt+]", "description": "Decrease / increase brush flow." },
                { "keys": "Ctrl+[ / Ctrl+]", "description": "Decrease / increase mask density." },
                { "keys": "Ctrl+Alt+[ / Ctrl+Alt+]", "description": "Decrease / increase mask opacity." },
                { "keys": "L / P", "description": "Start lasso or polygon selection." },
                { "keys": primaryAlt("M"), "description": "Open the masks workflow." },
                { "keys": primaryAlt("B"), "description": "Open brush masking from anywhere." },
                { "keys": "V", "description": "Cycle normal mask preview modes: Color Overlay, Marching Ants, Hidden." },
                { "keys": "H (hold)", "description": "Temporarily hide the active mask preview." },
                { "keys": "H", "description": "Start the heal brush." },
                { "keys": "Delete", "description": "Remove the active mask." },
                { "keys": "F2", "description": "Rename the active mask." },
                { "keys": "Tab / Shift+Tab", "description": "Cycle object candidates during smart select." }
            ]
        },
        {
            title: "Timeline",
            accent: theme.warning,
            items: [
                { "keys": primary("S"), "description": "Save the current timeline." },
                { "keys": "Space", "description": "Play or pause sequence playback." },
                { "keys": "Left / Right", "description": "Step back / forward one frame." },
                { "keys": "Home / End", "description": "Jump to start / end of timeline." },
                { "keys": "V", "description": "Select tool." },
                { "keys": "B", "description": "Razor tool." },
                { "keys": "Y", "description": "Slip tool." },
                { "keys": "N", "description": "Roll tool." },
                { "keys": "S", "description": "Toggle snapping." },
                { "keys": "M", "description": "Drop a sequence marker at the playhead." },
                { "keys": primary("= / -"), "description": "Zoom in / out." },
                { "keys": primary("0"), "description": "Zoom to fit all clips." },
                { "keys": primary("\\"), "description": "Split the selected clip at the playhead." },
                { "keys": "Delete / Backspace", "description": "Delete selected clip." },
                { "keys": primary("Shift+M"), "description": "Open the media browser." },
                { "keys": primary("Shift+T"), "description": "Add a video track." }
            ]
        },
        {
            title: "Carousel",
            accent: theme.success,
            items: [
                { "keys": primary("Shift+A"), "description": "Add a new slide." },
                { "keys": primary("["), "description": "Move the current slide earlier." },
                { "keys": primary("]"), "description": "Move the current slide later." },
                { "keys": primary("Shift+C"), "description": "Export the active carousel." }
            ]
        }
    ]

    Theme { id: theme }

    function primary(key) {
        return primaryLabel + "+" + key
    }

    function primaryAlt(key) {
        return primaryLabel + "+Alt+" + key
    }

    function coreWorkflowShortcutSignature() {
        return [
            "adjust:" + shortcutSequence("tool-adjust"),
            "crop:" + shortcutSequence("tool-crop"),
            "masks:" + shortcutSequence("tool-masks"),
            "brush:" + shortcutSequence("tool-brush"),
            "compare:" + shortcutSequence("tool-compare"),
            "export:" + shortcutSequence("tool-export")
        ].join("|")
    }

    function maskShortcutHelpSignature() {
        return [
            "create:Ctrl+Shift+M",
            "add-subtract-intersect:Ctrl+Shift+A/S/I",
            "invert:Ctrl+Shift+V",
            "overlay:Ctrl+Shift+O",
            "outline:Ctrl+Shift+U",
            "layer-mask-target-next:Ctrl+Shift+L/K",
            "layer-mask-target-previous:Ctrl+Alt+Shift+L/K",
            "base-image:Ctrl+Shift+B",
            "temporary-erase:X-hold",
            "temporary-hide:H-hold",
            "rename:F2"
        ].join("|")
    }

    function brushShortcutHelpSignature() {
        return [
            "brush-add:B",
            "brush-erase:E",
            "temporary-erase:X-hold",
            "size:[/]",
            "hardness:Shift+[/Shift+]",
            "flow:Alt+[/Alt+]",
            "density:Ctrl+[/Ctrl+]",
            "opacity:Ctrl+Alt+[/Ctrl+Alt+]"
        ].join("|")
    }

    function shortcutDialogWorkflowSignature() {
        return [
            "visible:" + dialogVisibilityContract,
            "surface:" + dialogSurfaceContract,
            "preserve:" + functionPreservationMap,
            "editable:" + editableShortcutCount,
            "presets:" + shortcutPresetCount,
            "results:" + shortcutSearchResultCount(),
            "conflicts:" + shortcutConflictCount(),
            "active-preset:" + activeShortcutPreset
        ].join("|")
    }

    function shortcutSequence(shortcutId) {
        for (let i = 0; i < editableShortcuts.length; ++i) {
            if (editableShortcuts[i].id === shortcutId)
                return String(editableShortcuts[i].sequence || "")
        }
        return ""
    }

    function shortcutConflictIds(sequence, excludingShortcutId) {
        const cleanSequence = String(sequence || "").trim().toLowerCase()
        const cleanExcludedId = String(excludingShortcutId || "")
        let conflicts = []
        if (cleanSequence.length === 0)
            return conflicts
        for (let i = 0; i < editableShortcuts.length; ++i) {
            const shortcut = editableShortcuts[i]
            if (shortcut.id !== cleanExcludedId
                    && String(shortcut.sequence || "").trim().toLowerCase() === cleanSequence)
                conflicts.push(shortcut.id)
        }
        return conflicts
    }

    function shortcutWouldConflict(shortcutId, sequence) {
        return shortcutConflictIds(sequence, shortcutId).length > 0
    }

    function shortcutConflictCount() {
        let count = 0
        for (let i = 0; i < editableShortcuts.length; ++i) {
            if (shortcutConflictIds(editableShortcuts[i].sequence, editableShortcuts[i].id).length > 0)
                count += 1
        }
        return count
    }

    function shortcutMatchesQuery(label, keys, query) {
        const cleanQuery = String(query || shortcutSearchText || "").trim().toLowerCase()
        if (cleanQuery.length === 0)
            return true
        return String(label || "").toLowerCase().indexOf(cleanQuery) !== -1
            || String(keys || "").toLowerCase().indexOf(cleanQuery) !== -1
    }

    function editableShortcutMatches(shortcut) {
        return shortcutMatchesQuery(shortcut.label + " " + shortcut.id, shortcut.sequence, shortcutSearchText)
    }

    function sectionShortcutMatchCount(section) {
        let count = 0
        for (let i = 0; i < section.items.length; ++i) {
            const item = section.items[i]
            if (shortcutMatchesQuery(item.description, item.keys, shortcutSearchText))
                count += 1
        }
        return count
    }

    function shortcutSearchResultCount() {
        let count = 0
        for (let i = 0; i < editableShortcuts.length; ++i) {
            if (editableShortcutMatches(editableShortcuts[i]))
                count += 1
        }
        for (let sectionIndex = 0; sectionIndex < shortcutSections.length; ++sectionIndex)
            count += sectionShortcutMatchCount(shortcutSections[sectionIndex])
        return count
    }

    function presetForId(presetId) {
        for (let i = 0; i < shortcutPresetMappings.length; ++i) {
            if (shortcutPresetMappings[i].id === presetId)
                return shortcutPresetMappings[i]
        }
        return null
    }

    function shortcutPresetNames() {
        let names = []
        for (let i = 0; i < shortcutPresetMappings.length; ++i)
            names.push(shortcutPresetMappings[i].label)
        return names
    }

    function shortcutPresetWouldConflict(presetId) {
        const preset = presetForId(String(presetId || ""))
        if (preset === null)
            return false
        let usedSequences = {}
        for (let i = 0; i < editableShortcuts.length; ++i) {
            const shortcut = editableShortcuts[i]
            const sequence = String(preset.sequences[shortcut.id] || shortcut.sequence || "").trim().toLowerCase()
            if (sequence.length === 0)
                continue
            if (usedSequences[sequence])
                return true
            usedSequences[sequence] = true
        }
        return false
    }

    function applyShortcutPreset(presetId) {
        const cleanPresetId = String(presetId || "")
        const preset = presetForId(cleanPresetId)
        if (preset === null) {
            shortcutEditorStatus = "Shortcut preset was not found."
            return false
        }
        if (shortcutPresetWouldConflict(cleanPresetId)) {
            shortcutEditorStatus = "Shortcut preset contains conflicts."
            return false
        }

        const shortcuts = editableShortcuts.slice(0)
        for (let i = 0; i < shortcuts.length; ++i) {
            const shortcut = shortcuts[i]
            shortcuts[i] = {
                "id": shortcut.id,
                "label": shortcut.label,
                "sequence": String(preset.sequences[shortcut.id] || shortcut.sequence || "").trim(),
                "defaultSequence": shortcut.defaultSequence
            }
        }
        editableShortcuts = shortcuts
        activeShortcutPreset = cleanPresetId
        shortcutEditorStatus = "Applied " + preset.label + " shortcuts."
        return true
    }

    function setShortcutSequence(shortcutId, sequence) {
        const cleanId = String(shortcutId || "")
        const cleanSequence = String(sequence || "").trim()
        if (cleanId.length === 0 || cleanSequence.length === 0) {
            shortcutEditorStatus = "Shortcut cannot be empty."
            return false
        }
        const conflicts = shortcutConflictIds(cleanSequence, cleanId)
        if (conflicts.length > 0) {
            shortcutEditorStatus = "Shortcut conflicts with " + conflicts.join(", ") + "."
            return false
        }

        const shortcuts = editableShortcuts.slice(0)
        for (let i = 0; i < shortcuts.length; ++i) {
            if (shortcuts[i].id === cleanId) {
                shortcuts[i] = {
                    "id": shortcuts[i].id,
                    "label": shortcuts[i].label,
                    "sequence": cleanSequence,
                    "defaultSequence": shortcuts[i].defaultSequence
                }
                editableShortcuts = shortcuts
                shortcutEditorStatus = "Shortcut updated."
                return true
            }
        }
        shortcutEditorStatus = "Shortcut was not found."
        return false
    }

    function resetShortcutSequence(shortcutId) {
        for (let i = 0; i < editableShortcuts.length; ++i) {
            if (editableShortcuts[i].id === shortcutId)
                return setShortcutSequence(shortcutId, editableShortcuts[i].defaultSequence)
        }
        shortcutEditorStatus = "Shortcut was not found."
        return false
    }

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Overlay.modal: Rectangle {
        color: theme.alpha(theme.ink950, 0.78)
    }

    background: PopupSurface {
        accent: theme.accentWarm400
        accentRail: false
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space18
        spacing: theme.space16

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Label {
                    Layout.fillWidth: true
                    text: "Keyboard Shortcuts"
                    color: theme.textStrong
                    font.family: theme.fontDisplay
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: macStyle
                          ? "Custom shortcuts use Command on macOS, alongside native shortcuts like Command+Comma for Preferences."
                          : "Custom shortcuts use Control on desktop platforms, alongside native shortcuts like Control+Comma for Preferences."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            TextField {
                objectName: "shortcutSearchField"
                Layout.preferredWidth: 240
                placeholderText: "Search shortcuts"
                text: root.shortcutSearchText
                selectByMouse: true
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                onTextEdited: root.shortcutSearchText = text
            }

            ActionButton {
                text: "Close"
                compact: true
                onClicked: root.close()
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, parent.width)
                spacing: theme.space16

                GlassSurface {
                    objectName: "editableShortcutsPanel"
                    Layout.fillWidth: true
                    radius: theme.radiusPanel
                    accent: theme.accentWarm400
                    accentRail: false
                    elevated: true
                    implicitHeight: editableBody.implicitHeight + theme.panelPadding * 2

                    ColumnLayout {
                        id: editableBody
                        anchors.fill: parent
                        anchors.margins: theme.panelPadding
                        spacing: theme.space12

                        Label {
                            Layout.fillWidth: true
                            text: "Editable Shortcuts"
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 16
                            font.weight: Font.DemiBold
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.space8

                            Label {
                                Layout.fillWidth: true
                                text: "Preset mappings"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: 11
                            }

                            Repeater {
                                model: root.shortcutPresetMappings

                                delegate: ActionButton {
                                    required property var modelData
                                    text: modelData.label
                                    compact: true
                                    minimumReadableWidth: 112
                                    enabled: root.activeShortcutPreset !== modelData.id
                                    onClicked: root.applyShortcutPreset(modelData.id)
                                }
                            }
                        }

                        Repeater {
                            model: root.editableShortcuts

                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                visible: root.editableShortcutMatches(modelData)
                                spacing: theme.space12

                                Label {
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 160
                                    text: modelData.label
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }

                                TextField {
                                    Layout.preferredWidth: 150
                                    Layout.minimumWidth: 150
                                    text: modelData.sequence
                                    selectByMouse: true
                                    color: theme.textStrong
                                    font.family: theme.fontMono
                                    font.pixelSize: 12
                                    onEditingFinished: root.setShortcutSequence(modelData.id, text)
                                }

                                ActionButton {
                                    text: "Reset"
                                    compact: true
                                    minimumReadableWidth: 72
                                    onClicked: root.resetShortcutSequence(modelData.id)
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: root.shortcutEditorStatus.length > 0
                            text: root.shortcutEditorStatus
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 11
                        }
                    }
                }

                Repeater {
                    model: root.shortcutSections

                    delegate: GlassSurface {
                        required property var modelData

                        Layout.fillWidth: true
                        visible: root.sectionShortcutMatchCount(modelData) > 0
                        radius: theme.radiusPanel
                        accent: modelData.accent
                        accentRail: false
                        elevated: true
                        implicitHeight: body.implicitHeight + theme.panelPadding * 2

                        ColumnLayout {
                            id: body
                            anchors.fill: parent
                            anchors.margins: theme.panelPadding
                            spacing: theme.space12

                            Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: modelData.items

                                delegate: GlassSurface {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    visible: root.shortcutMatchesQuery(modelData.description, modelData.keys, root.shortcutSearchText)
                                    radius: theme.radiusControl
                                    surfaceOpacity: 0.46
                                    edgeOpacity: 0.22
                                    implicitHeight: shortcutRow.implicitHeight + theme.space12 * 2

                                    RowLayout {
                                        id: shortcutRow
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space12

                                        Rectangle {
                                            Layout.alignment: Qt.AlignTop
                                            Layout.minimumWidth: 96
                                            Layout.preferredWidth: Math.min(220, Math.max(96, keyLabel.implicitWidth + 18))
                                            Layout.maximumWidth: 220
                                            radius: 7
                                            color: theme.alpha(theme.ink950, 0.34)
                                            border.width: 1
                                            border.color: theme.alpha(theme.polar, 0.16)
                                            implicitHeight: Math.max(34, keyLabel.implicitHeight + 12)

                                            Label {
                                                id: keyLabel
                                                anchors.fill: parent
                                                anchors.margins: 8
                                                text: modelData.keys
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: 12
                                                fontSizeMode: Text.HorizontalFit
                                                minimumPixelSize: 9
                                                font.weight: Font.DemiBold
                                                horizontalAlignment: Text.AlignHCenter
                                                verticalAlignment: Text.AlignVCenter
                                                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                                elide: Text.ElideNone
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.description
                                            color: theme.textMain
                                            font.family: theme.fontUi
                                            font.pixelSize: 12
                                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                            elide: Text.ElideNone
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
