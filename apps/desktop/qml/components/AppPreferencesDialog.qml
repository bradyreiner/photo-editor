pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 920
    height: 640
    padding: 0
    standardButtons: Dialog.NoButton

    property int currentPage: 0
    readonly property var pageTitles: ["General", "Timeline", "Storage"]

    Theme { id: theme }

    component PreferenceSwitch: Switch {
        id: control

        implicitWidth: 46
        implicitHeight: 26
        padding: 0

        indicator: Rectangle {
            implicitWidth: 46
            implicitHeight: 26
            radius: height / 2
            color: control.checked ? theme.accentWarm500 : theme.alpha(theme.ink750, 0.96)
            border.width: 1
            border.color: control.checked
                          ? theme.alpha(theme.accentWarm300, 0.56)
                          : theme.alpha(theme.lineSoft, 0.92)

            Rectangle {
                width: 20
                height: 20
                radius: 10
                y: 3
                x: control.checked ? parent.width - width - 3 : 3
                color: control.checked ? theme.ink950 : theme.textStrong

                Behavior on x {
                    NumberAnimation { duration: theme.durationFast }
                }
            }

            Behavior on color {
                ColorAnimation { duration: theme.durationFast }
            }
        }

        contentItem: Item {}
    }

    component PreferenceSection: Rectangle {
        id: section

        required property string title
        property string subtitle: ""
        default property alias contentData: body.data

        Theme { id: sectionTheme }

        Layout.fillWidth: true
        radius: sectionTheme.radiusPanel
        color: sectionTheme.alpha(sectionTheme.glassBase, 0.42)
        border.width: 1
        border.color: sectionTheme.alpha(sectionTheme.glassEdge, 0.2)
        implicitHeight: body.implicitHeight + sectionTheme.panelPadding * 2

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: sectionTheme.panelPadding
            spacing: sectionTheme.space14

            ColumnLayout {
                Layout.fillWidth: true
                spacing: sectionTheme.space4

                Label {
                    Layout.fillWidth: true
                    text: section.title
                    color: sectionTheme.textStrong
                    font.family: sectionTheme.fontUi
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    visible: section.subtitle.length > 0
                    text: section.subtitle
                    color: sectionTheme.textMuted
                    font.family: sectionTheme.fontUi
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    component PreferenceToggleRow: Rectangle {
        id: row

        required property string title
        required property string description
        required property bool checked
        signal toggled(bool checked)

        Theme { id: rowTheme }

        Layout.fillWidth: true
        radius: rowTheme.radiusControl
        color: rowTheme.alpha(rowTheme.glassBase, 0.36)
        border.width: 1
        border.color: rowTheme.alpha(rowTheme.glassEdge, 0.2)
        implicitHeight: 70

        RowLayout {
            anchors.fill: parent
            anchors.margins: rowTheme.space12
            spacing: rowTheme.space12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: rowTheme.space4

                Label {
                    Layout.fillWidth: true
                    text: row.title
                    color: rowTheme.textStrong
                    font.family: rowTheme.fontUi
                    font.pixelSize: 13
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: row.description
                    color: rowTheme.textMuted
                    font.family: rowTheme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            PreferenceSwitch {
                checked: row.checked
                onToggled: row.toggled(checked)
            }
        }
    }

    component StorageCard: Rectangle {
        id: card

        required property string label
        required property string path
        required property var openFn

        Theme { id: cardTheme }

        Layout.fillWidth: true
        radius: cardTheme.radiusPanel
        color: cardTheme.alpha(cardTheme.glassBase, 0.42)
        border.width: 1
        border.color: cardTheme.alpha(cardTheme.glassEdge, 0.2)
        implicitHeight: body.implicitHeight + cardTheme.panelPadding * 2

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: cardTheme.panelPadding
            spacing: cardTheme.space10

            RowLayout {
                Layout.fillWidth: true

                Label {
                    Layout.fillWidth: true
                    text: card.label
                    color: cardTheme.textStrong
                    font.family: cardTheme.fontUi
                    font.pixelSize: 13
                    font.weight: Font.DemiBold
                }

                ActionButton {
                    text: "Open"
                    compact: true
                    onClicked: card.openFn()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: cardTheme.radiusControl
                color: cardTheme.alpha(cardTheme.ink900, 0.82)
                border.width: 1
                border.color: cardTheme.alpha(cardTheme.lineSoft, 0.8)
                implicitHeight: pathField.implicitHeight + cardTheme.space12 * 2

                TextArea {
                    id: pathField
                    anchors.fill: parent
                    anchors.margins: cardTheme.space12
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: card.path
                    color: cardTheme.textMain
                    font.family: cardTheme.fontMono
                    font.pixelSize: 11
                    background: null
                    padding: 0
                }
            }
        }
    }

    function openAt(index) {
        currentPage = Math.max(0, Math.min(pageTitles.length - 1, index))
        open()
    }

    function pageAccent(index) {
        if (index === 0)
            return theme.accentWarm400
        if (index === 1)
            return theme.warning
        return theme.accentCool400
    }

    function pageEyebrow(index) {
        if (index === 0)
            return "Shell"
        if (index === 1)
            return "Editing"
        return "Runtime"
    }

    function pageSubtitle(index) {
        if (index === 0)
            return "Tune launch behavior, preview acceleration, and default workspace routing."
        if (index === 1)
            return "Set timeline defaults and restore workspace controls."
        return "Review the folders PhotoEditor uses for application data, cache, logs, and backups."
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

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.margins: theme.space18
        spacing: theme.space18

        GlassSurface {
            Layout.preferredWidth: 232
            Layout.fillHeight: true
            accent: theme.accentWarm400
            accentRail: false
            elevated: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space18
                spacing: theme.space18

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: theme.space6

                    Label {
                        Layout.fillWidth: true
                        text: "Preferences"
                        color: theme.textStrong
                        font.family: theme.fontDisplay
                        font.pixelSize: 28
                        font.weight: Font.DemiBold
                    }

                    Label {
                        Layout.fillWidth: true
                        text: "Launch behavior, preview acceleration, editing defaults, and runtime folders."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }

                Repeater {
                    model: root.pageTitles

                    delegate: Button {
                        id: pageButton

                        required property int index
                        required property string modelData

                        Layout.fillWidth: true
                        implicitHeight: 74
                        hoverEnabled: true
                        onClicked: root.currentPage = index

                        background: Rectangle {
                            radius: theme.radiusPanel
                            border.width: 1
                            border.color: root.currentPage === pageButton.index
                                          ? theme.alpha(root.pageAccent(pageButton.index), 0.58)
                                          : theme.alpha(theme.glassEdge, pageButton.hovered ? 0.42 : 0.22)
                            color: root.currentPage === pageButton.index
                                   ? theme.alpha(root.pageAccent(pageButton.index), 0.14)
                                   : (pageButton.hovered ? theme.alpha(theme.glassLift, 0.64) : theme.alpha(theme.glassBase, 0.38))

                            Behavior on color {
                                ColorAnimation { duration: theme.durationFast }
                            }

                            Behavior on border.color {
                                ColorAnimation { duration: theme.durationFast }
                            }
                        }

                        contentItem: RowLayout {
                            spacing: theme.space12

                            Rectangle {
                                Layout.alignment: Qt.AlignTop
                                implicitWidth: 6
                                implicitHeight: 6
                                radius: 1
                                color: root.pageAccent(pageButton.index)
                                opacity: root.currentPage === pageButton.index ? 1.0 : 0.34
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: theme.space4

                                Label {
                                    Layout.fillWidth: true
                                    text: pageButton.modelData
                                    color: root.currentPage === pageButton.index ? theme.textStrong : theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.pageSubtitle(pageButton.index)
                                    color: theme.textFaint
                                    font.family: theme.fontUi
                                    font.pixelSize: 11
                                    wrapMode: Text.WordWrap
                                    maximumLineCount: 2
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }

                GlassSurface {
                    Layout.fillWidth: true
                    radius: theme.radiusPanel
                    surfaceOpacity: 0.42
                    edgeOpacity: 0.2
                    implicitHeight: shellNote.implicitHeight + theme.space16 * 2

                    Label {
                        id: shellNote
                        anchors.fill: parent
                        anchors.margins: theme.space16
                        text: "Changes apply immediately and are persisted through app restarts."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }

        GlassSurface {
            Layout.fillWidth: true
            Layout.fillHeight: true
            accent: root.pageAccent(root.currentPage)
            elevated: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space18
                spacing: theme.space16

                Rectangle {
                    Layout.fillWidth: true
                    radius: theme.radiusPanel
                    color: theme.alpha(root.pageAccent(root.currentPage), 0.1)
                    border.width: 1
                    border.color: theme.alpha(root.pageAccent(root.currentPage), 0.3)
                    implicitHeight: heroBody.implicitHeight + theme.space18 * 2

                    ColumnLayout {
                        id: heroBody
                        anchors.fill: parent
                        anchors.margins: theme.space18
                        spacing: theme.space8

                        Label {
                            text: root.pageEyebrow(root.currentPage)
                            color: root.pageAccent(root.currentPage)
                            font.family: theme.fontMono
                            font.pixelSize: 11
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.pageTitles[root.currentPage]
                            color: theme.textStrong
                            font.family: theme.fontUi
                            font.pixelSize: 24
                            font.weight: Font.DemiBold
                        }

                        Label {
                            Layout.fillWidth: true
                            text: root.pageSubtitle(root.currentPage)
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.currentPage

                    ScrollView {
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: Math.max(0, parent.width)
                            spacing: theme.space16

                            PreferenceSection {
                                title: "Appearance"
                                subtitle: "Control the visual appearance of the application."

                                PreferenceToggleRow {
                                    title: "Dark Mode"
                                    description: "When disabled, the interface switches to a light color scheme."
                                    checked: appShell.darkMode
                                    onToggled: function(checked) {
                                        appShell.darkMode = checked
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Launch Behavior"
                                subtitle: "Choose where the app lands when it opens and whether it should resume your last workspace."

                                Label {
                                    Layout.fillWidth: true
                                    text: "Startup Workspace"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    Layout.fillWidth: true
                                    model: appShell.workspaceNames
                                    currentIndex: appShell.startupWorkspace
                                    onActivated: appShell.startupWorkspace = currentIndex
                                }

                                PreferenceToggleRow {
                                    title: "Reopen last workspace on launch"
                                    description: "When enabled, PhotoEditor returns to the last workspace you were using instead of the startup workspace above."
                                    checked: appShell.reopenLastWorkspace
                                    onToggled: function(checked) {
                                        appShell.reopenLastWorkspace = checked
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Preview Backend"
                                subtitle: "Choose how PhotoEditor handles interactive preview acceleration. CPU fallback always remains available."

                                Label {
                                    Layout.fillWidth: true
                                    text: "Preview Backend Preference"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: 12
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    Layout.fillWidth: true
                                    model: previewBackend.backendPreferenceOptions
                                    currentIndex: Math.max(0, model.indexOf(previewBackend.preferredBackend))
                                    onActivated: previewBackend.preferredBackend = currentText
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    radius: theme.radiusControl
                                    color: theme.alpha(theme.ink900, 0.82)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.8)
                                    implicitHeight: backendStatusBody.implicitHeight + theme.space12 * 2

                                    ColumnLayout {
                                        id: backendStatusBody
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space6

                                        Label {
                                            Layout.fillWidth: true
                                            text: "Active: " + previewBackend.activeBackend
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 13
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: previewBackend.backendSummary
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            wrapMode: Text.WordWrap
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: "Metal available: " + (previewBackend.metalAvailable ? "Yes" : "No")
                                                  + "  •  CUDA available: " + (previewBackend.cudaAvailable ? "Yes" : "No")
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    Label {
                                        Layout.fillWidth: true
                                        text: previewBackend.fallbackReason.length > 0
                                              ? previewBackend.fallbackReason
                                              : "Auto prefers Metal on Apple Silicon and CUDA on supported Windows NVIDIA systems."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }

                                    ActionButton {
                                        text: "Refresh"
                                        compact: true
                                        onClicked: previewBackend.refresh()
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Reset"
                                subtitle: "Restore the shell to its default startup behavior."

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    Label {
                                        Layout.fillWidth: true
                                        text: "This resets startup workspace, workspace restore behavior, and timeline defaults."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }

                                    ActionButton {
                                        text: "Restore Defaults"
                                        onClicked: appShell.restoreDefaults()
                                    }
                                }
                            }
                        }
                    }

                    ScrollView {
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: Math.max(0, parent.width)
                            spacing: theme.space16

                            PreferenceSection {
                                title: "Timeline Defaults"
                                subtitle: "Choose the editing behavior new timeline sessions should start with."

                                PreferenceToggleRow {
                                    title: "Enable snapping by default"
                                    description: "Snap trims, drags, and clip placement to nearby edit points and playhead positions."
                                    checked: appShell.defaultTimelineSnapping
                                    onToggled: function(checked) {
                                        appShell.defaultTimelineSnapping = checked
                                    }
                                }

                                PreferenceToggleRow {
                                    title: "Enable ripple trim by default"
                                    description: "Use ripple-aware timeline trims by default when sequence edits support it."
                                    checked: appShell.defaultTimelineRipple
                                    onToggled: function(checked) {
                                        appShell.defaultTimelineRipple = checked
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Workspace Reset"
                                subtitle: "Restore timeline panels, widths, tabs, snapping, and ripple state to defaults."

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Use this if the timeline layout gets too fragmented or you want to reapply the current defaults."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: 12
                                        wrapMode: Text.WordWrap
                                    }

                                    ActionButton {
                                        text: "Reset Timeline Workspace"
                                        onClicked: appShell.resetTimelineWorkspaceSettings()
                                    }
                                }
                            }
                        }
                    }

                    ScrollView {
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: Math.max(0, parent.width)
                            spacing: theme.space16

                            PreferenceSection {
                                title: "Runtime Paths"
                                subtitle: "These folders hold PhotoEditor application data, generated cache, logs, and local backups."

                                StorageCard {
                                    label: "Application Data"
                                    path: appShell.appDataLocation
                                    openFn: function() { appShell.openAppDataFolder() }
                                }

                                StorageCard {
                                    label: "Cache"
                                    path: appShell.cacheLocation
                                    openFn: function() { appShell.openCacheFolder() }
                                }

                                StorageCard {
                                    label: "Logs"
                                    path: appShell.logLocation
                                    openFn: function() { appShell.openLogFolder() }
                                }

                                StorageCard {
                                    label: "Backups"
                                    path: appShell.backupLocation
                                    openFn: function() { appShell.openBackupFolder() }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: theme.radiusPanel
                    color: theme.alpha(theme.ink850, 0.92)
                    border.width: 1
                    border.color: theme.alpha(theme.lineSoft, 0.52)
                    implicitHeight: footerRow.implicitHeight + theme.space12 * 2

                    RowLayout {
                        id: footerRow
                        anchors.fill: parent
                        anchors.margins: theme.space12
                        spacing: theme.space12

                        Label {
                            Layout.fillWidth: true
                            text: root.currentPage === 2
                                  ? "Storage locations are read-only here; use Open to reveal them in Finder."
                                  : "Preference changes are saved immediately."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }

                        ActionButton {
                            visible: root.currentPage === 2
                            text: "Open App Data"
                            compact: true
                            onClicked: appShell.openAppDataFolder()
                        }

                        ActionButton {
                            text: "Close"
                            compact: true
                            tone: "primary"
                            iconName: "close"
                            onClicked: root.close()
                        }
                    }
                }
            }
        }
    }
}
