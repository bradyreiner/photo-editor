pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "timelineToolPalette"
    Accessible.role: Accessible.ToolBar
    Accessible.name: qsTr("Timeline editing tools")

    required property var theme
    property string activeTool: "select"
    property var shortcutProvider: null
    property bool hasActiveTimeline: false
    property bool rateStretchEnabled: false
    property string rateStretchDisabledReason: "Select an unlocked clip without a retime curve."
    property bool envelopePenEnabled: false
    property string envelopePenDisabledReason: "Select an unlocked clip or track envelope."
    property int trackSelectionDirection: 1
    property bool vertical: false
    property int columns: vertical ? 1 : 8
    property bool compact: true
    property bool showHeading: false
    property bool showLabels: vertical
    property bool showCreationTools: vertical
    property bool condensed: false

    signal toolRequested(string tool)
    signal createRequested(string kind)

    readonly property string paletteContract: "timeline-tool-palette:v2|tools:select-track-select-forward-backward-blade-trim-ripple-roll-slip-slide-rate-stretch-pen-hand-zoom|create:text-shape|layout:single-icon-row-or-labeled-vertical|shortcut-hints:tooltip|state:active-and-disabled-reason|accessibility:labeled"
    readonly property var entries: [
        { key: "select", label: "Select", icon: "select", shortcut: "V", kind: "tool" },
        { key: "track-select-backward", label: "Track Select ←", icon: "step-back", shortcut: "Shift+A", kind: "direction", direction: -1 },
        { key: "track-select-forward", label: "Track Select →", icon: "step-forward", shortcut: "A", kind: "direction", direction: 1 },
        { key: "razor", label: "Blade", icon: "cut", shortcut: "C", kind: "tool" },
        { key: "trim", label: "Trim", icon: "trim", shortcut: "", kind: "tool" },
        { key: "ripple", label: "Ripple Trim", icon: "insert", shortcut: "", kind: "tool" },
        { key: "roll", label: "Roll", icon: "roll", shortcut: "N", kind: "tool" },
        { key: "slip", label: "Slip", icon: "slip", shortcut: "Y", kind: "tool" },
        { key: "slide", label: "Slide", icon: "slide", shortcut: "U", kind: "tool" },
        { key: "rate-stretch", label: "Rate Stretch", icon: "speed", shortcut: "R", kind: "tool" },
        { key: "pen", label: "Pen", icon: "curve", shortcut: "P", kind: "tool" },
        { key: "hand", label: "Hand", icon: "hand", shortcut: "H", kind: "tool" },
        { key: "zoom", label: "Zoom", icon: "zoom", shortcut: "Z", kind: "tool" },
        { key: "text", label: "Text", icon: "text", shortcut: "Create", kind: "create" },
        { key: "shape", label: "Shape", icon: "shape", shortcut: "Create", kind: "create" }
    ]
    readonly property int toolCount: entries.length
    readonly property var availableEntries: entries.filter(entry => root.showCreationTools || entry.kind !== "create")
    readonly property var primaryTools: ["select", "razor"]
    readonly property var visibleEntries: availableEntries.filter(entry =>
        !root.condensed || root.primaryTools.indexOf(entry.key) >= 0)
    readonly property var overflowEntries: root.condensed ? availableEntries.filter(entry =>
        root.primaryTools.indexOf(entry.key) < 0) : []
    readonly property int visibleToolCount: visibleEntries.length

    function displayLabel(entry) {
        const shortcut = inlineShortcut(entry)
        return shortcut.length > 0 && shortcut !== "Create"
                ? entry.label + " · " + shortcut
                : entry.label
    }

    function inlineShortcut(entry) {
        if (entry.kind === "direction" || entry.kind === "create")
            return ""
        const shortcut = String(shortcutFor(entry) || "")
        return shortcut.length <= 3 ? shortcut : ""
    }

    function shortcutTooltip(entry) {
        const shortcut = String(shortcutFor(entry) || "")
        if (shortcut.length > 0 && shortcut !== "Create")
            return qsTr("Keyboard shortcut: %1").arg(shortcut)
        return ""
    }

    function toolHelp(entry) {
        if (entry.key === "pen")
            return qsTr("Click or drag to add clip opacity or audio gain keyframes. Hold Shift to edit the track envelope.")
        if (entry.key === "rate-stretch")
            return qsTr("Drag the clip to change duration and speed while preserving its source range.")
        if (entry.key === "hand")
            return qsTr("Drag anywhere on a clip or envelope to pan the timeline.")
        if (entry.key === "zoom")
            return qsTr("Click to zoom at the pointer. Hold Option or Alt to zoom out.")
        return qsTr("Use the %1 tool.").arg(entry.label)
    }

    function disabledReason(entry) {
        if (!hasActiveTimeline)
            return "Create or open a timeline first."
        if (entry.key === "rate-stretch" && !rateStretchEnabled)
            return rateStretchDisabledReason
        if (entry.key === "pen" && !envelopePenEnabled)
            return envelopePenDisabledReason
        return ""
    }

    function shortcutFor(entry) {
        const commands = {
            "select": "timeline-select", "razor": "timeline-razor",
            "track-select-forward": "timeline-track-select",
            "track-select-backward": "timeline-track-select-backward",
            "roll": "timeline-roll", "slip": "timeline-slip", "slide": "timeline-slide",
            "rate-stretch": "timeline-rate-stretch", "pen": "timeline-pen",
            "hand": "timeline-hand", "zoom": "timeline-zoom-tool"
        }
        return shortcutProvider && commands[entry.key]
                ? shortcutProvider(commands[entry.key], entry.shortcut) : entry.shortcut
    }

    function isActive(entry) {
        if (entry.kind === "direction")
            return activeTool === "track-select" && trackSelectionDirection === entry.direction
        return entry.kind === "tool" && activeTool === entry.key
    }

    function activate(entry) {
        if (entry.kind === "create") {
            createRequested(entry.key)
            return
        }
        if (entry.kind === "direction")
            trackSelectionDirection = entry.direction
        toolRequested(entry.kind === "direction" ? "track-select" : entry.key)
    }

    spacing: theme.space2

    Label {
        visible: root.showHeading
        text: qsTr("Timeline tools")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        font.weight: theme.weightControl
        Layout.leftMargin: theme.space2
    }

    Flow {
        id: toolGrid
        objectName: "timelineToolGrid"
        Layout.fillWidth: true
        Layout.preferredHeight: implicitHeight
        spacing: theme.gapDense
        flow: Flow.LeftToRight

        Repeater {
            model: root.visibleEntries

            delegate: Item {
                id: toolCell
                required property var modelData
                readonly property string unavailableReason: root.disabledReason(modelData)
                readonly property bool isEnabled: unavailableReason.length === 0
                readonly property bool isActiveTool: root.isActive(modelData)

                width: root.vertical ? toolGrid.width : toolButton.implicitWidth
                height: root.compact ? theme.controlHeightDense : theme.controlHeight

                ActionButton {
                    id: toolButton
                    objectName: "timelineTool_" + toolCell.modelData.key
                    anchors.fill: parent
                    text: root.displayLabel(toolCell.modelData)
                    iconName: toolCell.modelData.icon
                    showText: root.showLabels
                    tone: "quiet"
                    active: toolCell.isActiveTool
                    enabled: toolCell.isEnabled
                    compact: root.compact
                    allowLabelWrap: false
                    minimumReadableWidth: 0
                    accent: theme.accentCool400
                    disabledReason: {
                        const shortcutHint = root.shortcutTooltip(toolCell.modelData)
                        return toolCell.isEnabled || shortcutHint.length === 0
                                ? toolCell.unavailableReason
                                : toolCell.unavailableReason + " " + shortcutHint
                    }
                    tooltipText: {
                        const base = toolCell.isEnabled
                                ? (toolCell.modelData.kind === "direction"
                                   ? qsTr("Select clips %1 on the chosen track. The timeline pointer applies this direction.")
                                     .arg(toolCell.modelData.direction < 0 ? qsTr("backward") : qsTr("forward"))
                                   : toolCell.modelData.kind === "create"
                                     ? qsTr("Create a new %1 clip.").arg(toolCell.modelData.key)
                                     : root.toolHelp(toolCell.modelData))
                                : toolCell.unavailableReason
                        const shortcutHint = root.shortcutTooltip(toolCell.modelData)
                        const namedHelp = toolCell.modelData.label + ". " + base
                        return shortcutHint.length > 0 ? namedHelp + " " + shortcutHint : namedHelp
                    }
                    accessibleName: toolCell.modelData.kind === "direction"
                                    ? toolCell.modelData.label
                                    : toolCell.modelData.kind === "create"
                                      ? qsTr("Create %1 clip").arg(toolCell.modelData.key)
                                      : qsTr("%1 tool").arg(toolCell.modelData.label)
                    onClicked: root.activate(toolCell.modelData)
                }

            }
        }

        MenuButton {
            objectName: "timelineMoreToolsButton"
            visible: root.overflowEntries.length > 0
            text: qsTr("More editing tools")
            showText: false
            iconName: {
                const active = root.overflowEntries.find(entry => root.isActive(entry))
                return active ? active.icon : "chevron-down"
            }
            active: root.overflowEntries.some(entry => root.isActive(entry))
            compact: root.compact
            tone: "quiet"
            primaryOpensMenu: true
            showMenuIndicator: false
            tooltipText: {
                const active = root.overflowEntries.find(entry => root.isActive(entry))
                return (active ? qsTr("Active tool: %1. ").arg(active.label) : "")
                + qsTr("Track selection, trim, ripple, roll, slip, slide, rate stretch, keyframes, and navigation tools.")
            }
            menuItems: root.overflowEntries.map(entry => ({
                key: entry.key, label: entry.label, iconName: entry.icon,
                shortcut: root.shortcutFor(entry), enabled: root.disabledReason(entry).length === 0,
                disabledReason: root.disabledReason(entry), checked: root.isActive(entry)
            }))
            onMenuItemTriggered: function(key) {
                const entry = root.overflowEntries.find(option => option.key === key)
                if (entry) root.activate(entry)
            }
        }
    }

}
