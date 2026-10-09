pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "appPreferencesDialog"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(theme.scaled(920), parent ? parent.width - 32 : theme.scaled(920))
    height: Math.min(theme.scaled(640), parent ? parent.height - 32 : theme.scaled(640))
    padding: 0
    standardButtons: Dialog.NoButton

    property int currentPage: 0
    property string displayCalibrationMessage: ""
    readonly property var pageTitles: ["General", "Color", "Timeline", "Storage", "Privacy & Support", "Account"]
    readonly property string dialogVisibilityContract: "dialog-visibility:v1|preferences|always:nav-current-page-close-by-escape|contextual:page-settings-storage-paths-backend-status|one-expansion:workspace-picker-backend-picker-folder-open|hover-focus:preference-navigation-controls|diagnostic:backend-availability-storage-paths|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v2|preferences|nav:scrollable-left|page:dominant|sections:scrollable|toggles:stable|paths:copyable-wrap|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v4|dark-mode:general-toggle|interface-scale:0.9-2.0|density:comfortable-balanced-compact|contrast:standard-high|motion:standard-reduced|color-vision:5-palettes|startup-workspace:general-combo|reopen-workspace:general-toggle|preview-backend:general-combo|timeline-defaults:timeline-page|storage-folders:storage-open-actions"

    Theme { id: theme }

    component PreferenceSwitch: Switch {
        id: control

        implicitWidth: theme.scaled(48)
        implicitHeight: theme.scaled(32)
        padding: 0
        Accessible.role: Accessible.CheckBox
        activeFocusOnTab: true

        indicator: Rectangle {
            implicitWidth: theme.scaled(48)
            implicitHeight: theme.scaled(32)
            radius: height / 2
            color: control.checked ? theme.accentWarm500 : theme.alpha(theme.ink750, 0.96)
            border.width: 1
            border.color: control.checked
                          ? theme.alpha(theme.accentWarm300, 0.56)
                          : theme.alpha(theme.lineSoft, 0.92)

            Rectangle {
                width: theme.scaled(22)
                height: theme.scaled(22)
                radius: height / 2
                y: Math.round((parent.height - height) / 2)
                x: control.checked ? parent.width - width - theme.scaled(4) : theme.scaled(4)
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
                    font.pixelSize: sectionTheme.typeSection
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    visible: section.subtitle.length > 0
                    text: section.subtitle
                    color: sectionTheme.textMuted
                    font.family: sectionTheme.fontUi
                    font.pixelSize: sectionTheme.typeBody
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
        implicitHeight: Math.max(rowTheme.controlHeightStandard, toggleRowContent.implicitHeight + rowTheme.space12 * 2)

        RowLayout {
            id: toggleRowContent
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
                    font.pixelSize: rowTheme.typeControl
                    font.weight: Font.Medium
                    wrapMode: Text.WordWrap
                }

                Label {
                    Layout.fillWidth: true
                    text: row.description
                    color: rowTheme.textMuted
                    font.family: rowTheme.fontUi
                    font.pixelSize: rowTheme.typeCaption
                    wrapMode: Text.WordWrap
                }
            }

            PreferenceSwitch {
                id: toggleControl
                checked: row.checked
                Accessible.name: row.title
                Accessible.description: row.description
                // Accessibility APIs may set the check state directly instead
                // of emitting Switch.toggled. Forward every genuine state
                // change while ignoring updates coming from the row binding.
                onCheckedChanged: {
                    if (toggleControl.checked !== row.checked)
                        row.toggled(toggleControl.checked)
                }
            }
        }
    }

    component StorageCard: Rectangle {
        id: card

        required property string label
        required property string path
        required property var openFn
        property string description: ""
        property var changeFn: null
        property var disableFn: null
        property bool changeEnabled: true

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
                    font.pixelSize: cardTheme.typeControl
                    font.weight: Font.DemiBold
                }

                ActionButton {
                    visible: card.disableFn !== null
                    text: "Disable"
                    compact: true
                    onClicked: card.disableFn()
                }

                ActionButton {
                    visible: card.changeFn !== null
                    text: card.path.length > 0 ? "Change" : "Choose"
                    compact: true
                    enabled: card.changeEnabled
                    onClicked: card.changeFn()
                }

                ActionButton {
                    text: "Open"
                    compact: true
                    enabled: card.path.length > 0
                    onClicked: card.openFn()
                }
            }

            Label {
                Layout.fillWidth: true
                visible: card.description.length > 0
                text: card.description
                color: cardTheme.textMuted
                font.family: cardTheme.fontUi
                font.pixelSize: cardTheme.typeCaption
                wrapMode: Text.WordWrap
            }

            Rectangle {
                Layout.fillWidth: true
                radius: cardTheme.radiusControl
                color: cardTheme.alpha(cardTheme.ink900, 0.82)
                border.width: 1
                border.color: cardTheme.alpha(cardTheme.lineSoft, 0.8)
                implicitHeight: pathField.implicitHeight + cardTheme.space12 * 2

                ToneTextArea {
                    id: pathField
                    objectName: "configuredStoragePathField"
                    accessibleLabel: "Configured storage path"
                    anchors.fill: parent
                    anchors.margins: cardTheme.space12
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: card.path.length > 0 ? card.path : "Not configured"
                    color: cardTheme.textMain
                    font.family: cardTheme.fontMono
                    font.pixelSize: cardTheme.typeCaption
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

    function choosePhotoLibraryLocation() {
        const location = catalogAssets.pickFolder("Choose Photo Library Location")
        if (location.length > 0)
            appShell.setPhotoLibraryLocation(location)
    }

    function chooseOriginalsBackupLocation() {
        const location = catalogAssets.pickFolder("Choose Originals Backup Location")
        if (location.length > 0)
            appShell.setOriginalsBackupLocation(location)
    }

    function chooseCatalogBackupLocation() {
        const location = catalogAssets.pickFolder("Choose Catalog Backup Location")
        if (location.length > 0)
            appShell.setCatalogBackupLocation(location)
    }

    function pageAccent(index) {
        if (index === 0)
            return theme.accentWarm400
        if (index === 1)
            return theme.accentCool400
        if (index === 2)
            return theme.warning
        if (index === 3)
            return theme.accentCool400
        return theme.success
    }

    function pageEyebrow(index) {
        if (index === 0)
            return "Shell"
        if (index === 1)
            return "Display"
        if (index === 2)
            return "Editing"
        if (index === 3)
            return "Runtime"
        return "Trust"
    }

    function pageSubtitle(index) {
        if (index === 5)
            return "Manage your purchase, trial, and offline access."
        if (index === 0)
            return "Tune launch behavior, preview acceleration, and default workspace routing."
        if (index === 1)
            return "Bind measured luminance values to the active physical display for trustworthy preview rendering."
        if (index === 2)
            return "Set timeline defaults and restore workspace controls."
        if (index === 3)
            return "Review the folders Looksy uses for application data, cache, logs, and backups."
        return "Control what leaves this device, create a reviewable support bundle, and read the product policies."
    }

    function loadDisplayTargetFields() {
        const target = editorPreview.systemMonitorDisplayTarget || ({})
        if (!peakLuminanceField.activeFocus)
            peakLuminanceField.text = Number(target.peakLuminanceNits || 100).toString()
        if (!blackLuminanceField.activeFocus)
            blackLuminanceField.text = Number(target.blackLuminanceNits || 0.005).toString()
        if (!referenceWhiteField.activeFocus)
            referenceWhiteField.text = Number(target.referenceWhiteNits || 100).toString()
        if (!surroundLuminanceField.activeFocus)
            surroundLuminanceField.text = Number(target.surroundLuminanceNits || 5).toString()
    }

    function applyDisplayCalibration() {
        const accepted = appShell.setDisplayCalibration(
            editorPreview.systemMonitorDisplayIdentity,
            calibrationIdentityField.text,
            Number(peakLuminanceField.text),
            Number(blackLuminanceField.text),
            Number(referenceWhiteField.text),
            Number(surroundLuminanceField.text))
        if (!accepted) {
            displayCalibrationMessage = "Could not save: enter a run identity and values where black < reference white ≤ peak."
            return
        }
        editorPreview.refreshDisplayTarget()
        displayCalibrationMessage = "Calibration saved and applied to this display."
    }

    function clearDisplayCalibration() {
        appShell.clearDisplayCalibration()
        editorPreview.refreshDisplayTarget()
        calibrationIdentityField.clear()
        displayCalibrationMessage = "Calibration cleared. Preview is using explicit reference defaults."
        loadDisplayTargetFields()
    }

    onOpened: {
        loadDisplayTargetFields()
        const firstPage = pageNavigation.itemAt(currentPage)
        if (firstPage)
            firstPage.forceActiveFocus(Qt.PopupFocusReason)
    }

    Connections {
        target: editorPreview
        function onSystemMonitorProfileChanged() {
            root.loadDisplayTargetFields()
        }
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

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.margins: theme.space18
        spacing: theme.space18

        GlassSurface {
            objectName: "preferencesNavigationPanel"
            Layout.preferredWidth: Math.min(theme.scaled(232), root.availableWidth * 0.28)
            Layout.fillHeight: true
            accent: theme.accentWarm400
            accentRail: false
            elevated: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space18
                spacing: theme.space18

                Label {
                    Layout.fillWidth: true
                    text: "Preferences"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeDialogTitle
                    font.weight: Font.DemiBold
                }
                ScrollView {
                    objectName: "preferencesNavigationScroll"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: availableWidth
                    clip: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ColumnLayout {
                        width: parent.width
                        spacing: theme.space4
                        Repeater {
                            id: pageNavigation
                            model: root.pageTitles

                            delegate: PreferenceNavRow {
                                required property int index
                                required property string modelData

                                objectName: "preferencesPageNav_" + index
                                pageIndex: index
                                title: modelData
                                subtitle: root.pageSubtitle(index)
                                accent: root.pageAccent(index)
                                selected: root.currentPage === index
                                showSubtitle: false
                                onActivated: function(pageIndex) {
                                    root.currentPage = pageIndex
                                }
                            }
                        }

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

                ColumnLayout {
                    objectName: "preferencesPageHeader"
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Label {
                        Layout.fillWidth: true
                        text: root.pageTitles[root.currentPage]
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeDialogTitle
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        text: root.pageSubtitle(root.currentPage)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        wrapMode: Text.WordWrap
                    }
                }

                StackLayout {
                    objectName: "preferencesPageContent"
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
                                title: "Accessibility & Layout"
                                subtitle: "Scale controls, reduce motion, increase contrast, and choose a color-safe selection palette."

                                Label {
                                    Layout.fillWidth: true
                                    text: "Interface Scale"
                                    color: theme.textStrong
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    objectName: "interfaceScaleCombo"
                                    Layout.fillWidth: true
                                    model: ["90%", "100%", "125%", "150%", "200%"]
                                    currentIndex: {
                                        const values = [0.9, 1.0, 1.25, 1.5, 2.0]
                                        let best = 0
                                        for (let index = 1; index < values.length; ++index) {
                                            if (Math.abs(values[index] - appShell.interfaceScale) <
                                                    Math.abs(values[best] - appShell.interfaceScale))
                                                best = index
                                        }
                                        return best
                                    }
                                    Accessible.name: "Interface scale"
                                    onActivated: appShell.interfaceScale = [0.9, 1.0, 1.25, 1.5, 2.0][currentIndex]
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Control density"
                                    color: theme.textStrong
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    objectName: "interfaceDensityCombo"
                                    Layout.fillWidth: true
                                    model: ["Comfortable", "Balanced", "Compact"]
                                    currentIndex: Math.max(0, ["comfortable", "balanced", "compact"].indexOf(appShell.interfaceDensity))
                                    Accessible.name: "Control density"
                                    onActivated: appShell.interfaceDensity = ["comfortable", "balanced", "compact"][currentIndex]
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Adjust control spacing across the app. Panel layouts and text scale stay independent."
                                    color: theme.textMuted
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }

                                PreferenceToggleRow {
                                    objectName: "highContrastToggle"
                                    title: "High contrast"
                                    description: "Strengthen text, dividers, and keyboard focus without changing photo pixels."
                                    checked: appShell.highContrast
                                    onToggled: function(checked) { appShell.highContrast = checked }
                                }

                                PreferenceToggleRow {
                                    objectName: "reducedMotionToggle"
                                    title: "Reduce motion"
                                    description: "Remove nonessential transitions and stop animated selection outlines."
                                    checked: appShell.reducedMotion
                                    onToggled: function(checked) { appShell.reducedMotion = checked }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: "Selection & Mask Colors"
                                    color: theme.textStrong
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    objectName: "colorVisionModeCombo"
                                    Layout.fillWidth: true
                                    model: ["Standard", "Protanopia safe", "Deuteranopia safe", "Tritanopia safe", "Grayscale"]
                                    currentIndex: Math.max(0, ["standard", "protanopia", "deuteranopia", "tritanopia", "grayscale"].indexOf(appShell.colorVisionMode))
                                    Accessible.name: "Selection and mask color palette"
                                    onActivated: appShell.colorVisionMode = ["standard", "protanopia", "deuteranopia", "tritanopia", "grayscale"][currentIndex]
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
                                    font.pixelSize: theme.typeBody
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    accessibleLabel: qsTr("Startup workspace")
                                    Layout.fillWidth: true
                                    model: appShell.workspaceNames
                                    currentIndex: appShell.startupWorkspace
                                    onActivated: appShell.startupWorkspace = currentIndex
                                }

                                PreferenceToggleRow {
                                    title: "Reopen last workspace on launch"
                                    description: "When enabled, Looksy returns to the last workspace you were using instead of the startup workspace above."
                                    checked: appShell.reopenLastWorkspace
                                    onToggled: function(checked) {
                                        appShell.reopenLastWorkspace = checked
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Preview Backend"
                                subtitle: "Choose how Looksy handles interactive preview acceleration. CPU fallback always remains available."

                                Label {
                                    Layout.fillWidth: true
                                    text: "Preview Backend Preference"
                                    color: theme.textStrong
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeBody
                                    font.weight: Font.Medium
                                }

                                ToneComboBox {
                                    accessibleLabel: qsTr("Preview backend preference")
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
                                            font.pixelSize: theme.typeControl
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: previewBackend.backendSummary
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.WordWrap
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: "Metal available: " + (previewBackend.metalAvailable ? "Yes" : "No")
                                                  + "  •  CUDA available: " + (previewBackend.cudaAvailable ? "Yes" : "No")
                                            color: theme.textFaint
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeMicro
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
                                        font.pixelSize: theme.typeBody
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
                                        font.pixelSize: theme.typeBody
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
                                title: "Active Display Target"
                                subtitle: "The ICC profile describes color primaries and transfer behavior. Measured luminance values below define the physical viewing target used by the display transform."

                                Rectangle {
                                    Layout.fillWidth: true
                                    radius: theme.radiusControl
                                    color: theme.alpha(theme.ink900, 0.82)
                                    border.width: 1
                                    border.color: editorPreview.systemMonitorDisplayTarget.verified
                                                  ? theme.alpha(theme.success, 0.56)
                                                  : theme.alpha(theme.warning, 0.56)
                                    implicitHeight: displayTargetStatusBody.implicitHeight + theme.space12 * 2

                                    ColumnLayout {
                                        id: displayTargetStatusBody
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space6

                                        Label {
                                            Layout.fillWidth: true
                                            text: editorPreview.systemMonitorDisplayTarget.verified
                                                  ? "Calibrated target"
                                                  : "Reference defaults — not measured"
                                            color: editorPreview.systemMonitorDisplayTarget.verified
                                                   ? theme.success
                                                   : theme.warning
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeControl
                                            font.weight: Font.DemiBold
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: "Display: " + (editorPreview.systemMonitorDisplayIdentity.length > 0
                                                                  ? editorPreview.systemMonitorDisplayIdentity
                                                                  : "No active display identity")
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: theme.typeMicro
                                            wrapMode: Text.WrapAnywhere
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: "ICC: " + (editorPreview.systemMonitorProfileName.length > 0
                                                              ? editorPreview.systemMonitorProfileName
                                                              : "Unavailable")
                                                  + "  •  Source: "
                                                  + editorPreview.systemMonitorDisplayTarget.provenance
                                            color: theme.textMuted
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Display Calibration"
                                subtitle: "Enter measurements from a named meter run. Values are stored only for the physical display identity shown above."

                                Label {
                                    Layout.fillWidth: true
                                    text: "Calibration / meter run identity"
                                    color: theme.textStrong
                                    font.pixelSize: theme.typeControl
                                    font.weight: Font.Medium
                                }

                                ToneTextField {
                                    id: calibrationIdentityField
                                    objectName: "displayCalibrationIdentityField"
                                    Layout.fillWidth: true
                                    placeholderText: "Example: i1DisplayPro-2026-07-29"
                                    Accessible.name: "Display calibration identity"
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 2
                                    columnSpacing: theme.space12
                                    rowSpacing: theme.space10

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Peak luminance (nits)"
                                            color: theme.textMuted
                                            font.pixelSize: theme.typeCaption
                                        }
                                        ToneTextField {
                                            id: peakLuminanceField
                                            objectName: "displayPeakLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 1; top: 10000; decimals: 6 }
                                            Accessible.name: "Peak luminance in nits"
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Black luminance (nits)"
                                            color: theme.textMuted
                                            font.pixelSize: theme.typeCaption
                                        }
                                        ToneTextField {
                                            id: blackLuminanceField
                                            objectName: "displayBlackLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 0; top: 100; decimals: 9 }
                                            Accessible.name: "Black luminance in nits"
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Reference white (nits)"
                                            color: theme.textMuted
                                            font.pixelSize: theme.typeCaption
                                        }
                                        ToneTextField {
                                            id: referenceWhiteField
                                            objectName: "displayReferenceWhiteField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 1; top: 10000; decimals: 6 }
                                            Accessible.name: "Reference white in nits"
                                        }
                                    }

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Surround luminance (nits)"
                                            color: theme.textMuted
                                            font.pixelSize: theme.typeCaption
                                        }
                                        ToneTextField {
                                            id: surroundLuminanceField
                                            objectName: "displaySurroundLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator { bottom: 0; top: 10000; decimals: 6 }
                                            Accessible.name: "Surround luminance in nits"
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: root.displayCalibrationMessage.length > 0
                                    text: root.displayCalibrationMessage
                                    color: root.displayCalibrationMessage.startsWith("Could not")
                                           ? theme.error
                                           : theme.textMuted
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    ActionButton {
                                        objectName: "clearDisplayCalibrationButton"
                                        text: "Clear Calibration"
                                        enabled: editorPreview.systemMonitorDisplayTarget.verified
                                        onClicked: root.clearDisplayCalibration()
                                    }

                                    Item { Layout.fillWidth: true }

                                    ActionButton {
                                        objectName: "applyDisplayCalibrationButton"
                                        text: "Apply to Active Display"
                                        tone: "primary"
                                        enabled: editorPreview.systemMonitorDisplayIdentity.length > 0
                                                 && calibrationIdentityField.text.trim().length > 0
                                                 && peakLuminanceField.acceptableInput
                                                 && blackLuminanceField.acceptableInput
                                                 && referenceWhiteField.acceptableInput
                                                 && surroundLuminanceField.acceptableInput
                                        onClicked: root.applyDisplayCalibration()
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
                                        font.pixelSize: theme.typeBody
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
                                title: "Photos & Import Backup"
                                subtitle: "Choose where managed originals live and, optionally, a separate destination for a verified second copy during import."

                                StorageCard {
                                    label: "Photo Library"
                                    description: "Managed imports are copied here. Leave in Place imports remain at their source."
                                    path: appShell.photoLibraryLocation
                                    openFn: function() { appShell.openPhotoLibraryFolder() }
                                    changeFn: function() { root.choosePhotoLibraryLocation() }
                                    changeEnabled: !appShell.photoLibraryMigrationRunning
                                }

                                StorageCard {
                                    label: "Originals Backup"
                                    description: appShell.originalsBackupEnabled
                                                 ? "Each import is copied here and verified with SHA-256 before the catalog reports success."
                                                 : "Optional. Choose a separate drive or folder to protect imported originals."
                                    path: appShell.originalsBackupLocation
                                    openFn: function() { appShell.openOriginalsBackupFolder() }
                                    changeFn: function() { root.chooseOriginalsBackupLocation() }
                                    disableFn: appShell.originalsBackupEnabled
                                               ? function() { appShell.disableOriginalsBackup() }
                                               : null
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: appShell.storageConfigurationStatus.length > 0
                                    text: appShell.storageConfigurationStatus
                                    color: theme.textMain
                                    font.family: theme.fontUi
                                    font.pixelSize: theme.typeCaption
                                    wrapMode: Text.WordWrap
                                }
                            }

                            PreferenceSection {
                                title: "Catalog & Runtime Paths"
                                subtitle: "The catalog stores edits and organization separately from original photo bytes."

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
                                    label: "Catalog Backups"
                                    description: "Checked database snapshots for projects, metadata, edits, and recovery. This is separate from the originals backup above."
                                    path: appShell.backupLocation
                                    openFn: function() { appShell.openBackupFolder() }
                                    changeFn: function() { root.chooseCatalogBackupLocation() }
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
                                title: "Private by default"
                                subtitle: "Photos and catalogs stay on this computer. You choose whether to send crash reports or share local diagnostic logs."

                                PreferenceToggleRow {
                                    title: "Send crash reports"
                                    description: typeof crashReporting !== "undefined"
                                                 ? crashReporting.status
                                                 : "Automatic crash reporting is not available in this version."
                                    checked: typeof crashReporting !== "undefined" && crashReporting.enabled
                                    enabled: typeof crashReporting !== "undefined" && crashReporting.available
                                    onToggled: function(checked) { crashReporting.enabled = checked }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: typeof crashReporting !== "undefined" && crashReporting.available
                                    text: "Reports go to Sentry and include app and system details, a customer ID when activated, and native crash data that may contain process memory. License keys and photo files are not deliberately attached. Turning this off removes queued reports."
                                    color: theme.textMuted
                                    font.pixelSize: theme.typeBody
                                    wrapMode: Text.WordWrap
                                }

                                PreferenceToggleRow {
                                    title: "Local diagnostic history"
                                    description: appShell.localDiagnosticsEnabled
                                                 ? "Enabled for this build. The latest sessions are retained locally for troubleshooting."
                                                 : "Disabled for this build."
                                    checked: appShell.localDiagnosticsEnabled
                                    enabled: false
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Create a support folder containing system metadata and up to three recent text-log sessions. Photo bytes are never included. Review the folder before sending it."
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        wrapMode: Text.WordWrap
                                    }

                                    ActionButton {
                                        text: "Create Support Bundle"
                                        onClicked: appShell.createSupportBundle()
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Policies and help"
                                subtitle: "These links open the current public policies in your browser."

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    ActionButton {
                                        text: "Support"
                                        onClicked: appShell.openSupportPage()
                                    }

                                    ActionButton {
                                        text: "Privacy"
                                        onClicked: appShell.openPrivacyPage()
                                    }

                                    ActionButton {
                                        text: "Terms"
                                        onClicked: appShell.openTermsPage()
                                    }

                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }
                    }
                    ScrollView {
                        clip: true
                        contentWidth: availableWidth
                        CustomerAccountPanel {
                            width: Math.max(0, parent.width)
                            controller: typeof customerAccess !== "undefined" ? customerAccess : null
                        }
                    }
                }

                Item {
                    objectName: "preferencesFooter"
                    Layout.fillWidth: true
                    implicitHeight: footerRow.implicitHeight

                    RowLayout {
                        id: footerRow
                        anchors.fill: parent
                        spacing: theme.space12

                        Label {
                            Layout.fillWidth: true
                            text: root.currentPage === 1
                                  ? "Only measured display values are labeled calibrated."
                                  : root.currentPage === 3
                                  ? "Original and catalog backups use separate folders."
                                  : root.currentPage === 4
                                    ? "Crash reports are optional. Review support bundles before sharing them."
                                  : "Preference changes are saved immediately."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }

                        ActionButton {
                            visible: root.currentPage === 3
                            text: "Open App Data"
                            compact: true
                            onClicked: appShell.openAppDataFolder()
                        }

                        ActionButton {
                            objectName: "preferencesCloseButton"
                            text: "Close"
                            compact: true
                            iconName: "close"
                            onClicked: root.close()
                        }
                    }
                }
            }
        }
    }
}
