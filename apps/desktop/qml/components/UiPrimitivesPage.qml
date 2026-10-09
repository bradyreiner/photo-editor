pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Window
import "UiPrimitiveCatalog.js" as Catalog

ApplicationWindow {
    id: root
    objectName: "uiPrimitivesWindow"
    title: "Looksy — UI Primitives"
    width: 1220
    height: 820
    minimumWidth: 1000
    minimumHeight: 650
    visible: false
    color: theme.ink950
    font.family: theme.fontUi
    font.pixelSize: theme.typeControl

    // A local creation context lets the real components preview Theme without
    // mutating the application controller, preferences, or active workspace.
    property string previewDensity: "balanced"
    property string previewFontFamily: Qt.font({ pixelSize: theme.typeControl }).family
    property bool previewReducedMotion: false
    property int sectionIndex: 0
    property string catalogQuery: ""
    property QtObject appShell: QtObject {
        property real interfaceScale: 1.0
        property string interfaceDensity: root.previewDensity
        readonly property bool reducedMotion: root.previewReducedMotion || !root.visible
        property bool highContrast: false
        property string colorVisionMode: "standard"
        property string systemUiFontFamily: root.previewFontFamily
    }
    readonly property string renderedDensity: theme.interfaceDensity
    readonly property var sections: [
        { title: "Foundations", detail: "Brand, type, color, spacing, and density", note: "Keep photographic content central. Give every surface and state a purpose." },
        { title: "Commands", detail: "Buttons, tools, menus, and selection", note: "Move the pointer, press, and Tab through these actual controls to review their response." },
        { title: "Inputs & inspectors", detail: "Fields, sliders, and section hierarchy", note: "Try typed values, keyboard entry, resets, and collapsed sections before choosing geometry." },
        { title: "Navigation & feedback", detail: "Panels, status, progress, and empty states", note: "Selection, information, and actions should have distinct visual roles." },
        { title: "Icons", detail: "The shared AppIcon vocabulary", note: "Use one stroke language. Pair unfamiliar icons with a visible label." },
        { title: "Component index", detail: "Inventory and the implementation plan", note: "Live specimens cover the shared families. Contextual compositions still require workspace review." }
    ]
    readonly property var filteredCatalog: Catalog.components.filter(function(entry) {
        const query = root.catalogQuery.trim().toLowerCase()
        return !query || (entry.name + " " + entry.family + " " + entry.status).toLowerCase().indexOf(query) >= 0
    })

    Theme { id: theme }

    function open() {
        show()
        raise()
        requestActivate()
    }

    Shortcut { sequence: "Escape"; onActivated: root.close() }
    Shortcut { sequences: [StandardKey.Close]; onActivated: root.close() }

    header: Rectangle {
        color: theme.ink900
        implicitHeight: headerControls.implicitHeight + theme.space18 * 2
        ColumnLayout {
            id: headerControls
            anchors.fill: parent
            anchors.margins: theme.space18
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { text: "LOOKSY / UI PRIMITIVES"; font.family: theme.fontUi; font.pixelSize: theme.typeCaption; font.letterSpacing: 1.5; color: theme.textMuted }
                Item { Layout.fillWidth: true }
                Text { text: "Live component library"; color: theme.textMuted; font.pixelSize: theme.typeCaption }
                ActionButton { objectName: "uiPrimitivesCloseButton"; text: "Close"; tone: "quiet"; compact: true; onClicked: root.close() }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Preview density"; color: theme.textMain; font.pixelSize: theme.typeControl }
                Repeater {
                    model: ["comfortable", "balanced", "compact"]
                    delegate: FilterChip {
                        required property string modelData
                        objectName: "uiPrimitivesDensity-" + modelData
                        text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                        active: root.previewDensity === modelData
                        onClicked: root.previewDensity = modelData
                    }
                }
                Item { Layout.fillWidth: true }
                ToneCheckBox { objectName: "uiPrimitivesReducedMotion"; text: "Reduced motion"; checked: root.previewReducedMotion; onToggled: root.previewReducedMotion = checked }
                ToneCheckBox { objectName: "uiPrimitivesHighContrast"; text: "High contrast"; checked: root.appShell.highContrast; onToggled: root.appShell.highContrast = checked }
                Text { text: "Preview only · 100%"; color: theme.textFaint; font.pixelSize: theme.typeCaption }
            }
        }
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: theme.lineSoft }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.preferredWidth: 210
            Layout.fillHeight: true
            color: theme.ink900
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 4
                Repeater {
                    model: root.sections
                    delegate: ActionButton {
                        required property var modelData
                        required property int index
                        objectName: "uiPrimitivesSection-" + index
                        Layout.fillWidth: true
                        text: modelData.title
                        tone: "quiet"
                        active: root.sectionIndex === index
                        onClicked: root.sectionIndex = index
                    }
                }
                Item { Layout.fillHeight: true }
                Text {
                    Layout.fillWidth: true
                    text: "Design direction\n\nTechnical geometry. Quiet surfaces. Clear hierarchy. Responsive states."
                    color: theme.textMuted
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WordWrap
                    lineHeight: 1.3
                }
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: theme.lineSoft }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 24
            spacing: 12
            Text { Layout.fillWidth: true; text: root.sections[root.sectionIndex].title; color: theme.textStrong; font.pixelSize: theme.typePageTitle; font.weight: Font.DemiBold }
            Text { Layout.fillWidth: true; text: root.sections[root.sectionIndex].detail; color: theme.textMain; font.pixelSize: theme.typeSection }
            Text { Layout.fillWidth: true; text: root.sections[root.sectionIndex].note; color: theme.textMuted; font.pixelSize: theme.typeControl; wrapMode: Text.WordWrap }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.lineSoft }
            ScrollView {
                id: specimenScroll
                objectName: "uiPrimitivesScroll"
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical.policy: ScrollBar.AlwaysOn
                Loader {
                    id: specimenLoader
                    objectName: "uiPrimitivesSpecimenLoader"
                    width: Math.max(0, specimenScroll.availableWidth - theme.space16)
                    sourceComponent: [foundationsComponent, commandsComponent, inputsComponent, feedbackComponent, iconsComponent, indexComponent][root.sectionIndex]
                    onLoaded: specimenScroll.contentItem.contentY = 0
                }
            }
            Text { Layout.fillWidth: true; text: "Changes here are local examples. Use the component index to plan adoption across the workspaces."; color: theme.textFaint; font.pixelSize: theme.typeCaption; wrapMode: Text.WordWrap }
        }
    }

    Component { id: commandsComponent; UiPrimitivesCommands {} }
    Component { id: inputsComponent; UiPrimitivesInputs {} }
    Component { id: feedbackComponent; UiPrimitivesFeedback {} }

    Component {
        id: foundationsComponent
        ColumnLayout {
            objectName: "uiPrimitivesFoundations"
            spacing: 20
            UiPrimitivesOpticalDetails { Layout.fillWidth: true }
            Text { text: "The direction"; color: theme.textStrong; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: "Build a desktop editing studio around a neutral photographic canvas, restrained rectangular controls, and a consistent type and spacing scale. Reserve color for meaning. Make hover, press, selection, and keyboard focus readable without decorating every control."
                color: theme.textMain; font.pixelSize: theme.typeBody; wrapMode: Text.WordWrap; lineHeight: 1.4
            }
            Text { text: "Color roles · live Theme tokens"; color: theme.textStrong; font.pixelSize: theme.typeSection }
            GridLayout {
                Layout.fillWidth: true
                columns: 4
                columnSpacing: 16
                rowSpacing: 12
                Repeater {
                    model: [
                        { name: "Canvas", value: theme.mediaCanvas }, { name: "Panel", value: theme.ink900 },
                        { name: "Field", value: theme.fieldFill }, { name: "Hover", value: theme.editorControlHover },
                        { name: "Primary action", value: theme.actionPrimaryFill }, { name: "Focus", value: theme.focusRing },
                        { name: "Main text", value: theme.textMain }, { name: "Muted text", value: theme.textMuted },
                        { name: "Optical light", value: theme.opticalLight }, { name: "Brand coral", value: theme.brandCoral },
                        { name: "Error", value: theme.brandRed }, { name: "Separator", value: theme.lineSoft }
                    ]
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 5
                        Rectangle { Layout.fillWidth: true; implicitHeight: 32; color: modelData.value; radius: theme.radiusControl; border.color: theme.lineSoft }
                        Text { text: modelData.name; color: theme.textMain; font.pixelSize: theme.typeCaption }
                        Text { text: String(modelData.value).toUpperCase(); color: theme.textFaint; font.family: theme.fontMono; font.pixelSize: theme.typeMicro }
                    }
                }
            }
            Text { text: "Type hierarchy"; color: theme.textStrong; font.pixelSize: theme.typeSection }
            Repeater {
                model: [
                    { label: "Page title", size: theme.typePageTitle, weight: theme.weightSection },
                    { label: "Section heading", size: theme.typeSection, weight: theme.weightSection },
                    { label: "Control label", size: theme.typeControl, weight: theme.weightControl },
                    { label: "Body text", size: theme.typeBody, weight: Font.Normal },
                    { label: "Caption / supporting detail", size: theme.typeCaption, weight: Font.Normal }
                ]
                delegate: RowLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    Text { Layout.fillWidth: true; text: modelData.label; color: theme.textMain; font.family: theme.fontUi; font.pixelSize: modelData.size; font.weight: modelData.weight }
                    Text { text: modelData.size + " px"; color: theme.textMuted; font.family: theme.fontMono; font.pixelSize: theme.typeCaption }
                }
            }
            Text { text: "Density geometry · " + root.previewDensity; color: theme.textStrong; font.pixelSize: theme.typeSection }
            Text {
                objectName: "uiPrimitivesGeometryReadout"
                Layout.fillWidth: true
                text: "Control " + theme.controlHeight + " px   /   Field " + theme.controlHeightTall + " px   /   Panel inset " + theme.panelPadding + " px   /   Related gap " + theme.gapRelated + " px   /   Primary target " + theme.controlHeightPrimary + " px"
                color: theme.textMain; font.family: theme.fontMono; font.pixelSize: theme.typeCaption; wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "Comfortable: more breathing room. Balanced: precise desktop controls. Compact: more tools and tracks on screen. Labels retain the same readable type size; padding and targets change with density. Interface scaling remains available."
                color: theme.textMuted; font.pixelSize: theme.typeBody; wrapMode: Text.WordWrap
            }
        }
    }

    Component {
        id: iconsComponent
        GridLayout {
            objectName: "uiPrimitivesIcons"
            columns: Math.max(4, Math.floor(width / 135))
            columnSpacing: 12
            rowSpacing: 16
            Repeater {
                model: Catalog.iconNames
                delegate: ColumnLayout {
                    required property string modelData
                    Layout.fillWidth: true
                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: theme.space8
                        AppIcon { name: modelData; implicitWidth: 14; implicitHeight: 14; color: theme.textMuted }
                        AppIcon { name: modelData; implicitWidth: 18; implicitHeight: 18; color: theme.textMain }
                        AppIcon { name: modelData; implicitWidth: 24; implicitHeight: 24; color: theme.textMain }
                    }
                    Text { Layout.fillWidth: true; text: modelData; color: theme.textMuted; font.pixelSize: theme.typeCaption; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.WrapAnywhere }
                }
            }
        }
    }

    Component {
        id: indexComponent
        ColumnLayout {
            objectName: "uiPrimitivesIndex"
            spacing: 14
            Text { text: "Next design decisions"; color: theme.textStrong; font.pixelSize: theme.typeSection; font.weight: Font.DemiBold }
            Text {
                Layout.fillWidth: true
                text: "1. Use the shared platform font, restrained corners, and semantic surfaces shown here.\n2. Prefer flat inspector sections; use nested disclosure only where it helps scanning.\n3. Replace remaining bespoke styling with these shared controls where the role matches.\n4. Review complete workflows at all three densities: import media, trim, move clips, play, adjust color, and export."
                color: theme.textMain; font.pixelSize: theme.typeBody; wrapMode: Text.WordWrap; lineHeight: 1.5
            }
            Text {
                Layout.fillWidth: true
                text: "Acceptance: readable hierarchy; clear selected and focus states; discoverable tooltips; useful disabled reasons; keyboard operation; reduced motion; no overlap or clipped labels. A live specimen is an implementation reference, not completed workspace qualification."
                color: theme.textMuted; font.pixelSize: theme.typeBody; wrapMode: Text.WordWrap
            }
            ToneTextField { objectName: "uiPrimitivesCatalogSearch"; Layout.fillWidth: true; placeholderText: "Find a component, family, or coverage state"; text: root.catalogQuery; onTextEdited: root.catalogQuery = text }
            Text { objectName: "uiPrimitivesCatalogCount"; text: root.filteredCatalog.length + " / " + Catalog.components.length + " components · shared QML inventory"; color: theme.textMuted; font.pixelSize: theme.typeCaption }
            Repeater {
                model: root.filteredCatalog
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Text { Layout.fillWidth: true; text: modelData.name + ".qml"; color: theme.textMain; font.family: theme.fontMono; font.pixelSize: theme.typeCaption; wrapMode: Text.WrapAnywhere }
                        Text { text: modelData.status; color: theme.textMuted; font.pixelSize: theme.typeCaption }
                    }
                    Text { Layout.fillWidth: true; text: modelData.family + " · " + modelData.plan; color: theme.textFaint; font.pixelSize: theme.typeCaption; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: theme.lineSoft }
                }
            }
            Text { visible: root.filteredCatalog.length === 0; text: "No matching components. Try a name such as Slider or a family such as Feedback."; color: theme.textMuted; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
    }
}
