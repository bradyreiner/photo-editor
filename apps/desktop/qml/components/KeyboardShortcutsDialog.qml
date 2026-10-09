pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "keyboardShortcutsDialog"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(theme.scaled(860), parent ? parent.width - 32 : theme.scaled(860))
    height: Math.min(theme.scaled(620), parent ? parent.height - 32 : theme.scaled(620))
    padding: 0
    standardButtons: Dialog.NoButton

    readonly property bool macStyle: Qt.platform.os === "osx"
    readonly property string primaryLabel: macStyle ? "Cmd" : "Ctrl"
    readonly property string redoLabel: macStyle ? "Cmd+Shift+Z" : "Ctrl+Y"
    property string shortcutEditorStatus: ""
    property string activeShortcutPreset: "app-native"
    property string shortcutSearchText: ""
    property var editableShortcuts: [
        { "id": "command-palette", "label": "Command Palette", "sequence": primarySequence("K"), "defaultSequence": primarySequence("K") },
        { "id": "workspace-library", "label": "Go To Library", "sequence": primarySequence("1"), "defaultSequence": primarySequence("1") },
        { "id": "workspace-edit", "label": "Go To Edit", "sequence": primarySequence("2"), "defaultSequence": primarySequence("2") },
        { "id": "tool-adjust", "label": "Adjust Tool", "sequence": primarySequenceAlt("A"), "defaultSequence": primarySequenceAlt("A") },
        { "id": "tool-crop", "label": "Crop Tool", "sequence": primarySequenceAlt("C"), "defaultSequence": primarySequenceAlt("C") },
        { "id": "tool-masks", "label": "Masks Tool", "sequence": primarySequenceAlt("M"), "defaultSequence": primarySequenceAlt("M") },
        { "id": "tool-brush", "label": "Brush Tool", "sequence": primarySequenceAlt("B"), "defaultSequence": primarySequenceAlt("B") },
        { "id": "tool-compare", "label": "Compare Tool", "sequence": primarySequenceAlt("P"), "defaultSequence": primarySequenceAlt("P") },
        { "id": "tool-export", "label": "Export Tool", "sequence": primarySequenceAlt("E"), "defaultSequence": primarySequenceAlt("E") },
        { "id": "queue-export", "label": "Start Export", "sequence": primarySequence("Shift+E"), "defaultSequence": primarySequence("Shift+E") },
        { "id": "toggle-full-edit", "label": "Full-Screen Edit", "sequence": "F", "defaultSequence": "F" }
    ]
    readonly property int editableShortcutCount: editableShortcuts.length
    readonly property string dialogVisibilityContract: "dialog-visibility:v2|keyboard-shortcuts|always:title-search-close|contextual:matching-editable-shortcuts-matching-reference-sections-presets-without-query-status-conflicts-empty-search-platform-modifier|one-expansion:preset-apply-editable-sequence-reset|hover-focus:preset-buttons-edit-fields-reset-controls|diagnostic:conflict-count-search-results|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v2|keyboard-shortcuts|search:header|editable:matching|sections:scrollable-matching|keys:scaled-wrapped|descriptions:wrap|presets:wrapping-buttons|empty:explicit|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v2|search-shortcuts:header-field|edit-sequence:inline-field-persisted|reset-sequence:inline-reset|apply-presets:preset-buttons-persisted|conflict-detection:dialog-and-controller|runtime-actions:live-bindings|platform-modifiers:mac-or-control|shortcut-help:section-cards"
    readonly property var shortcutPresetMappings: [
        {
            "id": "app-native",
            "label": "App Native",
            "sequences": {
                "command-palette": primarySequence("K"),
                "workspace-library": primarySequence("1"),
                "workspace-edit": primarySequence("2"),
                "tool-adjust": primarySequenceAlt("A"),
                "tool-crop": primarySequenceAlt("C"),
                "tool-masks": primarySequenceAlt("M"),
                "tool-brush": primarySequenceAlt("B"),
                "tool-compare": primarySequenceAlt("P"),
                "tool-export": primarySequenceAlt("E"),
                "queue-export": primarySequence("Shift+E"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "lightroom",
            "label": "Lightroom",
            "sequences": {
                "command-palette": primarySequence("K"),
                "workspace-library": "G",
                "workspace-edit": "D",
                "tool-adjust": primarySequenceAlt("A"),
                "tool-crop": primarySequenceAlt("C"),
                "tool-masks": primarySequenceAlt("M"),
                "tool-brush": primarySequenceAlt("B"),
                "tool-compare": primarySequenceAlt("P"),
                "tool-export": primarySequenceAlt("E"),
                "queue-export": primarySequence("Shift+E"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "photoshop",
            "label": "Photoshop",
            "sequences": {
                "command-palette": primarySequence("Alt+K"),
                "workspace-library": "F5",
                "workspace-edit": "F6",
                "tool-adjust": primarySequenceAlt("A"),
                "tool-crop": primarySequenceAlt("C"),
                "tool-masks": primarySequenceAlt("M"),
                "tool-brush": primarySequenceAlt("B"),
                "tool-compare": primarySequenceAlt("P"),
                "tool-export": primarySequenceAlt("E"),
                "queue-export": primarySequence("Alt+Shift+S"),
                "toggle-full-edit": "F"
            }
        },
        {
            "id": "capture-one",
            "label": "Capture One",
            "sequences": {
                "command-palette": primarySequence("K"),
                "workspace-library": primarySequence("B"),
                "workspace-edit": primarySequence("E"),
                "tool-adjust": primarySequenceAlt("A"),
                "tool-crop": primarySequenceAlt("C"),
                "tool-masks": primarySequenceAlt("M"),
                "tool-brush": primarySequenceAlt("B"),
                "tool-compare": primarySequenceAlt("P"),
                "tool-export": primarySequenceAlt("E"),
                "queue-export": primarySequence("D"),
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
                { "keys": "Shift+click", "description": "Select a range of Library photos from the last clicked photo." },
                { "keys": primary("click"), "description": "Add or remove individual Library photos from the selection." },
                { "keys": primary("C"), "description": "In Library or Edit, choose edit groups to copy from the active photo. Ctrl+C also works on Mac." },
                { "keys": primary("V"), "description": "Paste copied edit groups onto selected photos. Ctrl+V also works on Mac." },
                { "keys": primary("O"), "description": "Import a folder into the library." },
                { "keys": primary("N"), "description": "Create a new timeline." },
                { "keys": primary("1-5"), "description": "Switch directly between Library, Edit, Boards in Edit, Timeline, and Export." },
                { "keys": primary("E"), "description": "Open the selected photo in Edit. Unavailable when a video or timeline is selected." },
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
                { "keys": "J / K / L", "description": "Shuttle reverse, stop, or forward; repeat J or L for 2x, 4x, and 8x." },
                { "keys": "Left / Right", "description": "Step back / forward one frame." },
                { "keys": "Home / End", "description": "Jump to start / end of timeline." },
                { "keys": "Alt+Left / Alt+Right", "description": "Jump to the previous / next edit point." },
                { "keys": "Alt+Up / Alt+Down", "description": "Jump to the previous / next sequence marker." },
                { "keys": "Shift+I / Shift+O", "description": "Go to the Source In / Out point." },
                { "keys": "F", "description": "Match the Program frame into the Source monitor." },
                { "keys": "V", "description": "Select tool." },
                { "keys": "C", "description": "Razor tool." },
                { "keys": "B", "description": "Split the selected clip at the playhead." },
                { "keys": "Y", "description": "Slip tool." },
                { "keys": "N", "description": "Roll tool." },
                { "keys": "U", "description": "Slide tool." },
                { "keys": "S", "description": "Toggle snapping." },
                { "keys": "M", "description": "Drop a sequence marker at the playhead." },
                { "keys": primary("C / X / V"), "description": "Copy, cut, or paste selected timeline clips." },
                { "keys": primary("Shift+V"), "description": "Insert-paste clips and ripple every track plus markers." },
                { "keys": primary("D"), "description": "Duplicate selected clips with track spacing and linked audio." },
                { "keys": primaryAlt("R"), "description": "Replace selected clip media from Source without changing sequence timing." },
                { "keys": primary("= / -"), "description": "Zoom in / out." },
                { "keys": primary("0"), "description": "Zoom to fit all clips." },
                { "keys": primary("\\"), "description": "Split the selected clip at the playhead." },
                { "keys": "Delete / Backspace", "description": "Delete selected clip." },
                { "keys": primary("Shift+M"), "description": "Open the media browser." },
                { "keys": primary("Shift+T"), "description": "Add a video track." }
            ]
        },
        {
            title: "Boards in Edit",
            accent: theme.success,
            items: [
                { "keys": primary("Z"), "description": "Undo the last board edit." },
                { "keys": primary("Shift+Z"), "description": "Redo the last undone edit." },
                { "keys": primary("C / X / V"), "description": "Copy, cut, or paste selected layers." },
                { "keys": primary("D"), "description": "Duplicate selected layers." },
                { "keys": primary("G / Shift+G"), "description": "Group or ungroup selected layers." },
                { "keys": "V / H / T / M / U / B / E", "description": "Select, pan, text, media, shape, brush, or eraser tool." },
                { "keys": primary("Shift+A"), "description": "Add a new slide." },
                { "keys": primary("["), "description": "Move the current slide earlier." },
                { "keys": primary("]"), "description": "Move the current slide later." },
                { "keys": primary("Shift+C"), "description": "Export the active board document." }
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

    function primarySequence(key) {
        return "Ctrl+" + key
    }

    function primarySequenceAlt(key) {
        return "Ctrl+Alt+" + key
    }

    function normalizeShortcutSequence(sequence) {
        return String(sequence || "").trim().replace(/(^|\+)Cmd(?=\+)/g, "$1Ctrl")
    }

    function displayShortcutSequence(sequence) {
        const normalized = normalizeShortcutSequence(sequence)
        return macStyle ? normalized.replace(/(^|\+)Ctrl(?=\+)/g, "$1Cmd") : normalized
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
                return displayShortcutSequence(editableShortcuts[i].sequence)
        }
        return ""
    }

    function shortcutConflictIds(sequence, excludingShortcutId) {
        const cleanSequence = normalizeShortcutSequence(sequence).toLowerCase()
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
        return shortcutMatchesQuery(shortcut.label + " " + shortcut.id,
                                    displayShortcutSequence(shortcut.sequence),
                                    shortcutSearchText)
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
    function editableShortcutMatchCount() {
        let count = 0
        for (let index = 0; index < editableShortcuts.length; ++index)
            if (editableShortcutMatches(editableShortcuts[index])) count += 1
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
            const sequence = normalizeShortcutSequence(preset.sequences[shortcut.id] || shortcut.sequence || "").toLowerCase()
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
                "sequence": normalizeShortcutSequence(preset.sequences[shortcut.id] || shortcut.sequence || ""),
                "defaultSequence": shortcut.defaultSequence
            }
        }
        let overrides = {}
        for (let index = 0; index < shortcuts.length; ++index)
            overrides[shortcuts[index].id] = shortcuts[index].sequence
        if (!appShell.setShortcutOverrides(overrides)) {
            shortcutEditorStatus = "Shortcut preset could not be saved because it conflicts with another shortcut."
            return false
        }
        editableShortcuts = shortcuts
        activeShortcutPreset = cleanPresetId
        shortcutEditorStatus = "Applied " + preset.label + " shortcuts."
        return true
    }

    function setShortcutSequence(shortcutId, sequence) {
        const cleanId = String(shortcutId || "")
        const cleanSequence = normalizeShortcutSequence(sequence)
        if (cleanId.length === 0 || cleanSequence.length === 0) {
            shortcutEditorStatus = "Shortcut cannot be empty."
            return false
        }
        const conflicts = shortcutConflictIds(cleanSequence, cleanId)
        if (conflicts.length > 0) {
            shortcutEditorStatus = "Shortcut conflicts with " + conflicts.join(", ") + "."
            return false
        }

        if (!appShell.setShortcutOverride(cleanId, cleanSequence)) {
            shortcutEditorStatus = "Shortcut could not be saved because it conflicts with another shortcut."
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
            if (editableShortcuts[i].id === shortcutId) {
                appShell.resetShortcutOverride(shortcutId)
                const shortcuts = editableShortcuts.slice(0)
                shortcuts[i] = {
                    "id": shortcuts[i].id,
                    "label": shortcuts[i].label,
                    "sequence": shortcuts[i].defaultSequence,
                    "defaultSequence": shortcuts[i].defaultSequence
                }
                editableShortcuts = shortcuts
                shortcutEditorStatus = "Shortcut reset."
                return true
            }
        }
        shortcutEditorStatus = "Shortcut was not found."
        return false
    }

    function loadPersistedShortcuts() {
        const shortcuts = editableShortcuts.slice(0)
        for (let index = 0; index < shortcuts.length; ++index) {
            shortcuts[index] = {
                "id": shortcuts[index].id,
                "label": shortcuts[index].label,
                "sequence": normalizeShortcutSequence(appShell.shortcutSequence(shortcuts[index].id, shortcuts[index].defaultSequence)),
                "defaultSequence": shortcuts[index].defaultSequence
            }
        }
        editableShortcuts = shortcuts
    }

    Component.onCompleted: loadPersistedShortcuts()
    onOpened: {
        shortcutSearchText = ""
        shortcutSearchField.forceActiveFocus(Qt.PopupFocusReason)
    }

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Overlay.modal: Rectangle {
        color: theme.alpha(theme.ink950, 0.78)
    }

    background: PopupSurface {
        radius: theme.radiusPanel
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
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeDialogTitle
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: "Edit mappings or search the shortcut reference."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeControl
                    wrapMode: Text.WordWrap
                }
            }

            ToneTextField {
                id: shortcutSearchField
                objectName: "shortcutSearchField"
                accessibleLabel: "Search keyboard shortcuts"
                Layout.preferredWidth: Math.min(theme.scaled(240), root.availableWidth * 0.32)
                placeholderText: "Search shortcuts"
                text: root.shortcutSearchText
                selectByMouse: true
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                onTextEdited: root.shortcutSearchText = text
            }

            ActionButton {
                objectName: "keyboardShortcutsCloseButton"
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

                Label {
                    objectName: "keyboardShortcutsNoResults"
                    Layout.fillWidth: true
                    visible: root.shortcutSearchText.trim().length > 0 && root.shortcutSearchResultCount() === 0
                    text: "No shortcuts match \u201c" + root.shortcutSearchText + "\u201d."
                    textFormat: Text.PlainText
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.WordWrap
                }

                GlassSurface {
                    objectName: "editableShortcutsPanel"
                    visible: root.editableShortcutMatchCount() > 0
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
                            font.pixelSize: theme.typeSection
                            font.weight: Font.DemiBold
                        }

                        ColumnLayout {
                            visible: root.shortcutSearchText.trim().length === 0
                            Layout.fillWidth: true
                            spacing: theme.space8

                            Label {
                                Layout.fillWidth: true
                                text: "Preset mappings"
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: theme.space8
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
                                    Layout.minimumWidth: theme.scaled(160)
                                    text: modelData.label
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeControl
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                }

                                ToneTextField {
                                    objectName: "shortcutSequenceField_" + String(modelData.id)
                                    accessibleLabel: "Shortcut for " + String(modelData.label)
                                    Layout.preferredWidth: theme.scaled(150)
                                    Layout.minimumWidth: theme.scaled(150)
                                    text: root.displayShortcutSequence(modelData.sequence)
                                    selectByMouse: true
                                    color: theme.textStrong
                                    font.family: theme.fontMono
                                    font.pixelSize: theme.typeControl
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
                            font.pixelSize: theme.typeCaption
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
                                font.pixelSize: theme.typeSection
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
                                            Layout.minimumWidth: theme.scaled(96)
                                            Layout.preferredWidth: Math.min(theme.scaled(220), Math.max(theme.scaled(96), keyLabel.implicitWidth + theme.space18))
                                            Layout.maximumWidth: theme.scaled(220)
                                            radius: theme.radiusControl
                                            color: theme.alpha(theme.ink950, 0.34)
                                            border.width: 1
                                            border.color: theme.alpha(theme.polar, 0.16)
                                            implicitHeight: Math.max(theme.controlHeightDense, keyLabel.implicitHeight + theme.space8 * 2)

                                            Label {
                                                id: keyLabel
                                                anchors.fill: parent
                                                anchors.margins: theme.space8
                                                text: modelData.keys
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: theme.typeControl
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
                                            font.pixelSize: theme.typeControl
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
