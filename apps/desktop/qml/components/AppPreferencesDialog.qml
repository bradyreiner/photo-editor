pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppDialog {
    id: root
    objectName: "appPreferencesDialog"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: Math.min(popupAvailableWidth, Math.max(1, Math.min(theme.scaled(920), parent ? parent.width - theme.scaled(32) : theme.scaled(920))))
    height: Math.min(popupAvailableHeight, Math.max(1, Math.min(theme.scaled(640), parent ? parent.height - theme.scaled(32) : theme.scaled(640))))
    padding: 0
    standardButtons: Dialog.NoButton

    property int currentPage: 0
    property string displayCalibrationMessage: ""
    property string searchQuery: ""
    readonly property var pageSearchTerms: ["appearance interface scale control density high contrast reduce motion selection mask colors startup launch reopen last workspace general", "color display monitor ICC calibration luminance white black nits profile", "timeline snapping ripple trim defaults editing", "storage photos import library originals backup catalog backups folders", "privacy support crash reports diagnostics history logs policy terms help", "account license licence trial purchase offline activation", "system details preview backend Metal GPU CPU acceleration application data cache logs runtime folders", "AI models search SigLIP assistant editing Ollama selection SAM inpaint LaMa download local status connection"]
    readonly property string searchNeedle: searchQuery.trim().toLowerCase()
    readonly property bool searchHasMatches: {
        for (let index = 0; index < pageTitles.length; ++index) {
            if (pageMatchesSearch(index))
                return true;
        }
        return false;
    }

    function matchesSearch(text) {
        const haystack = String(text || "").toLowerCase();
        return searchNeedle.length === 0 || searchNeedle.split(/\s+/).every(function (word) {
            return haystack.indexOf(word) >= 0;
        });
    }
    function pageMatchesSearch(index) {
        return matchesSearch(String(pageTitles[index] || "") + " " + String(pageSearchTerms[index] || ""));
    }
    function sectionMatchesSearch(title, subtitle, keywords) {
        return matchesSearch(String(pageTitles[currentPage] || "")) || matchesSearch(title + " " + subtitle + " " + keywords);
    }
    onSearchNeedleChanged: {
        if (!pageMatchesSearch(currentPage)) {
            for (let index = 0; index < pageTitles.length; ++index) {
                if (pageMatchesSearch(index)) {
                    currentPage = index;
                    break;
                }
            }
        }
    }
    readonly property var pageTitles: ["General", "Color", "Timeline", "Storage", "Privacy & Support", "Account", "System Details", "Models"]
    readonly property string dialogVisibilityContract: "dialog-visibility:v1|preferences|always:nav-current-page-close-by-escape|contextual:page-settings-storage-paths-backend-status|one-expansion:workspace-picker-backend-picker-folder-open|hover-focus:preference-navigation-controls|diagnostic:backend-availability-storage-paths|preserve:functionality"
    readonly property string dialogSurfaceContract: "dialog-surface:v2|preferences|nav:scrollable-left|page:dominant|sections:scrollable|toggles:stable|paths:copyable-wrap|no-elide:true|no-overlap:true"
    readonly property string functionPreservationMap: "function-preservation:v4|dark-mode:general-toggle|interface-scale:0.9-2.0|density:comfortable-balanced-compact|contrast:standard-high|motion:standard-reduced|color-vision:5-palettes|startup-workspace:general-combo|reopen-workspace:general-toggle|preview-backend:general-combo|timeline-defaults:timeline-page|storage-folders:storage-open-actions"

    Theme {
        id: theme
    }

    component PreferenceSection: Rectangle {
        id: section

        required property string title
        property string subtitle: ""
        property string searchTerms: ""
        visible: root.sectionMatchesSearch(title, subtitle, searchTerms)
        default property alias contentData: body.data

        Theme {
            id: sectionTheme
        }

        Layout.fillWidth: true
        radius: 0
        color: "transparent"
        border.width: sectionTheme.highContrast ? 1 : 0
        border.color: sectionTheme.alpha(sectionTheme.glassEdge, 0.2)
        implicitHeight: body.implicitHeight + sectionTheme.sectionPadding * 2

        ColumnLayout {
            id: body
            anchors.fill: parent
            anchors.margins: sectionTheme.sectionPadding
            spacing: sectionTheme.gapControl

            ColumnLayout {
                Layout.fillWidth: true
                spacing: sectionTheme.space4

                Label {
                    id: sectionTitle
                    Layout.fillWidth: true
                    text: section.title
                    color: sectionTheme.textStrong
                    font.family: sectionTheme.fontUi
                    font.pixelSize: sectionTheme.typeSection
                    font.weight: Font.DemiBold
                    Accessible.name: section.title
                    Accessible.description: section.subtitle
                    HoverHandler {
                        id: sectionTitleHover
                    }
                    AppToolTip {
                        targetItem: sectionTitle
                        visible: section.subtitle.length > 0 && sectionTitleHover.hovered
                        text: section.subtitle
                    }
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

        Theme {
            id: rowTheme
        }
        HoverHandler {
            id: toggleHelpHover
        }
        AppToolTip {
            targetItem: row
            visible: (toggleHelpHover.hovered || toggleControl.visualFocus) && row.description.length > 0
            text: row.description
        }

        Layout.fillWidth: true
        radius: rowTheme.radiusControl
        color: "transparent"
        border.width: rowTheme.highContrast ? 1 : 0
        border.color: rowTheme.alpha(rowTheme.glassEdge, 0.2)
        implicitHeight: Math.max(rowTheme.controlHeightStandard, toggleRowContent.implicitHeight + rowTheme.space6 * 2)

        RowLayout {
            id: toggleRowContent
            anchors.fill: parent
            anchors.margins: rowTheme.space6
            spacing: rowTheme.gapControl

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
                    visible: false
                    text: row.description
                    color: rowTheme.textMuted
                    font.family: rowTheme.fontUi
                    font.pixelSize: rowTheme.typeCaption
                    wrapMode: Text.WordWrap
                }
            }

            ToneSwitch {
                id: toggleControl
                checked: row.checked
                Accessible.name: row.title
                Accessible.description: row.description
                // Accessibility APIs may set the check state directly instead
                // of emitting Switch.toggled. Forward every genuine state
                // change while ignoring updates coming from the row binding.
                onCheckedChanged: {
                    if (toggleControl.checked !== row.checked)
                        row.toggled(toggleControl.checked);
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
        property bool detailsExpanded: false
        readonly property string folderName: String(path || "").replace(/[\\/]+$/, "").split(/[\\/]/).pop() || "Not configured"

        Theme {
            id: cardTheme
        }
        Layout.fillWidth: true
        color: "transparent"
        implicitHeight: body.implicitHeight

        ColumnLayout {
            id: body
            anchors.fill: parent
            spacing: cardTheme.space4

            GridLayout {
                Layout.fillWidth: true
                // Keep folder actions beside the label at enlarged scale when
                // the dialog still has enough room for a comfortable row.
                columns: width >= cardTheme.scaled(360) ? 2 : 1
                columnSpacing: cardTheme.space8
                rowSpacing: cardTheme.space4
                Label {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: card.label
                    color: cardTheme.textStrong
                    font.family: cardTheme.fontUi
                    font.pixelSize: cardTheme.typeControl
                    font.weight: Font.Medium
                    wrapMode: Text.Wrap
                }
                RowLayout {
                    Layout.alignment: Qt.AlignRight
                    spacing: cardTheme.space2
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
                    ActionButton {
                        objectName: "storagePathDetailsButton"
                        text: "Details"
                        compact: true
                        active: card.detailsExpanded
                        onClicked: card.detailsExpanded = !card.detailsExpanded
                    }
                }
            }
            Label {
                id: storageFolderLabel
                objectName: "configuredStorageFolderLabel"
                Layout.fillWidth: true
                text: card.folderName
                color: cardTheme.textMuted
                font.family: cardTheme.fontUi
                font.pixelSize: cardTheme.typeCaption
                wrapMode: Text.Wrap
                Accessible.name: card.label + ": " + card.folderName
                Accessible.description: card.path
                HoverHandler {
                    id: storageFolderHover
                }
                AppToolTip {
                    targetItem: storageFolderLabel
                    visible: storageFolderHover.hovered
                    text: card.description + (card.description.length > 0 ? "\n" : "") + card.path
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                visible: card.detailsExpanded
                spacing: cardTheme.space4
                Label {
                    Layout.fillWidth: true
                    visible: card.description.length > 0
                    text: card.description
                    color: cardTheme.textMuted
                    font.family: cardTheme.fontUi
                    font.pixelSize: cardTheme.typeCaption
                    wrapMode: Text.Wrap
                }
                ToneTextArea {
                    objectName: "configuredStoragePathField"
                    accessibleLabel: card.label + " full path"
                    Layout.fillWidth: true
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.WrapAnywhere
                    text: card.path.length > 0 ? card.path : "Not configured"
                    font.family: cardTheme.fontUi
                    font.pixelSize: cardTheme.typeCaption
                }
                ActionButton {
                    objectName: "copyStoragePathButton"
                    text: "Copy path"
                    compact: true
                    enabled: card.path.length > 0
                    onClicked: appShell.copyTextToClipboard(card.path)
                }
            }
        }
    }

    function openAt(index) {
        searchQuery = "";
        currentPage = Math.max(0, Math.min(pageTitles.length - 1, index));
        open();
    }

    function choosePhotoLibraryLocation() {
        const location = catalogAssets.pickFolder("Choose Photo Library Location");
        if (location.length > 0)
            appShell.setPhotoLibraryLocation(location);
    }

    function chooseOriginalsBackupLocation() {
        const location = catalogAssets.pickFolder("Choose Originals Backup Location");
        if (location.length > 0)
            appShell.setOriginalsBackupLocation(location);
    }

    function chooseCatalogBackupLocation() {
        const location = catalogAssets.pickFolder("Choose Catalog Backup Location");
        if (location.length > 0)
            appShell.setCatalogBackupLocation(location);
    }

    function pageAccent(index) {
        if (index === 0)
            return theme.accentWarm400;
        if (index === 1)
            return theme.accentCool400;
        if (index === 2)
            return theme.warning;
        if (index === 3)
            return theme.accentCool400;
        return theme.success;
    }

    function pageEyebrow(index) {
        if (index === 0)
            return "Shell";
        if (index === 1)
            return "Display";
        if (index === 2)
            return "Editing";
        if (index === 3)
            return "Runtime";
        return "Trust";
    }

    function pageSubtitle(index) {
        if (index === 7)
            return "Manage local models for photo search, AI editing, selection, and object removal.";
        if (index === 6)
            return "Preview acceleration, runtime folders, and technical information.";
        if (index === 5)
            return "Manage your purchase, trial, and offline access.";
        if (index === 0)
            return "Tune launch behavior, preview acceleration, and default workspace routing.";
        if (index === 1)
            return "Bind measured luminance values to the active physical display for trustworthy preview rendering.";
        if (index === 2)
            return "Set timeline defaults and restore workspace controls.";
        if (index === 3)
            return "Review the folders Looksy uses for application data, cache, logs, and backups.";
        return "Control what leaves this device, create a reviewable support bundle, and read the product policies.";
    }

    function loadDisplayTargetFields() {
        const target = editorPreview.systemMonitorDisplayTarget || ({});
        if (!peakLuminanceField.activeFocus)
            peakLuminanceField.text = Number(target.peakLuminanceNits || 100).toString();
        if (!blackLuminanceField.activeFocus)
            blackLuminanceField.text = Number(target.blackLuminanceNits || 0.005).toString();
        if (!referenceWhiteField.activeFocus)
            referenceWhiteField.text = Number(target.referenceWhiteNits || 100).toString();
        if (!surroundLuminanceField.activeFocus)
            surroundLuminanceField.text = Number(target.surroundLuminanceNits || 5).toString();
    }

    function applyDisplayCalibration() {
        const accepted = appShell.setDisplayCalibration(editorPreview.systemMonitorDisplayIdentity, calibrationIdentityField.text, Number(peakLuminanceField.text), Number(blackLuminanceField.text), Number(referenceWhiteField.text), Number(surroundLuminanceField.text));
        if (!accepted) {
            displayCalibrationMessage = "Could not save: enter a run identity and values where black < reference white ≤ peak.";
            return;
        }
        editorPreview.refreshDisplayTarget();
        displayCalibrationMessage = "Calibration saved and applied to this display.";
    }

    function clearDisplayCalibration() {
        appShell.clearDisplayCalibration();
        editorPreview.refreshDisplayTarget();
        calibrationIdentityField.clear();
        displayCalibrationMessage = "Calibration cleared. Preview is using explicit reference defaults.";
        loadDisplayTargetFields();
    }

    onOpened: {
        loadDisplayTargetFields();
        const firstPage = pageNavigation.itemAt(currentPage);
        if (firstPage)
            firstPage.forceActiveFocus(Qt.PopupFocusReason);
    }

    Connections {
        target: editorPreview
        function onSystemMonitorProfileChanged() {
            root.loadDisplayTargetFields();
        }
    }

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    background: PopupSurface {}

    contentItem: RowLayout {
        anchors.fill: parent
        anchors.margins: theme.panelPadding
        spacing: theme.gapPanel

        GlassSurface {
            objectName: "preferencesNavigationPanel"
            Layout.preferredWidth: Math.min(theme.scaled(196), root.availableWidth * 0.28)
            Layout.fillHeight: true
            accent: theme.accentWarm400
            accentRail: false
            elevated: false

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.panelPadding
                spacing: theme.gapPanel

                Label {
                    Layout.fillWidth: true
                    text: "Preferences"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeDialogTitle
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space2
                    ToneTextField {
                        id: settingsSearch
                        objectName: "preferencesSearchField"
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        placeholderText: "Search settings"
                        Accessible.name: "Search settings"
                        text: root.searchQuery
                        onTextChanged: root.searchQuery = text
                        Keys.onEscapePressed: function (event) {
                            if (text.length > 0) {
                                clear();
                                event.accepted = true;
                            } else
                                event.accepted = false;
                        }
                    }
                    ActionButton {
                        objectName: "preferencesClearSearchButton"
                        visible: root.searchQuery.length > 0
                        showText: false
                        iconName: "close"
                        compact: true
                        tooltipText: "Clear search"
                        onClicked: { root.searchQuery = ""; settingsSearch.forceActiveFocus(Qt.TabFocusReason) }
                    }
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
                                visible: root.pageMatchesSearch(index)
                                title: modelData
                                subtitle: root.pageSubtitle(index)
                                accent: root.pageAccent(index)
                                selected: root.currentPage === index
                                showSubtitle: false
                                onActivated: function (pageIndex) {
                                    root.currentPage = pageIndex;
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
            elevated: false

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.panelPadding
                spacing: theme.gapUnrelated

                ColumnLayout {
                    objectName: "preferencesPageHeader"
                    visible: root.searchHasMatches
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
                        visible: false
                        text: root.pageSubtitle(root.currentPage)
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        wrapMode: Text.WordWrap
                    }
                }

                Label {
                    objectName: "preferencesNoSearchResults"
                    visible: !root.searchHasMatches
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    text: "No settings match “" + root.searchQuery + "”."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    wrapMode: Text.Wrap
                    verticalAlignment: Text.AlignVCenter
                }
                StackLayout {
                    objectName: "preferencesPageContent"
                    visible: root.searchHasMatches
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: root.currentPage

                    ScrollView {
                        clip: true
                        contentWidth: availableWidth

                        ColumnLayout {
                            width: Math.max(0, parent.width)
                            spacing: theme.gapUnrelated

                            PreferenceSection {
                                title: "Appearance & accessibility"
                                searchTerms: "appearance interface scale control density high contrast reduce motion selection mask colors"
                                subtitle: "Text size, control spacing, contrast, motion, and selection colors."

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= theme.scaled(360) ? 2 : 1
                                    columnSpacing: theme.space12
                                    rowSpacing: theme.space4
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: "Interface scale"
                                        color: theme.textStrong
                                        font.pixelSize: theme.typeControl
                                        font.weight: Font.Medium
                                    }
                                    ToneComboBox {
                                        objectName: "interfaceScaleCombo"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        model: ["90%", "100%", "125%", "150%", "200%"]
                                        currentIndex: {
                                            const values = [0.9, 1.0, 1.25, 1.5, 2.0];
                                            let best = 0;
                                            for (let index = 1; index < values.length; ++index) {
                                                if (Math.abs(values[index] - appShell.interfaceScale) < Math.abs(values[best] - appShell.interfaceScale))
                                                    best = index;
                                            }
                                            return best;
                                        }
                                        Accessible.name: "Interface scale"
                                        onActivated: appShell.interfaceScale = [0.9, 1.0, 1.25, 1.5, 2.0][currentIndex]
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: "Control spacing"
                                        color: theme.textStrong
                                        font.pixelSize: theme.typeControl
                                        font.weight: Font.Medium
                                    }
                                    ToneComboBox {
                                        objectName: "interfaceDensityCombo"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        model: ["Comfortable", "Balanced", "Compact"]
                                        currentIndex: Math.max(0, ["comfortable", "balanced", "compact"].indexOf(appShell.interfaceDensity))
                                        Accessible.name: "Control spacing"
                                        Accessible.description: "Changes the spacing of controls. Text size is independent."
                                        onActivated: appShell.interfaceDensity = ["comfortable", "balanced", "compact"][currentIndex]
                                    }
                                }

                                PreferenceToggleRow {
                                    objectName: "highContrastToggle"
                                    title: "High contrast"
                                    description: "Strengthen text, dividers, and keyboard focus without changing photo pixels."
                                    checked: appShell.highContrast
                                    onToggled: function (checked) {
                                        appShell.highContrast = checked;
                                    }
                                }

                                PreferenceToggleRow {
                                    objectName: "reducedMotionToggle"
                                    title: "Reduce motion"
                                    description: "Remove nonessential transitions and stop animated selection outlines."
                                    checked: appShell.reducedMotion
                                    onToggled: function (checked) {
                                        appShell.reducedMotion = checked;
                                    }
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
                                searchTerms: "startup launch reopen workspace"
                                subtitle: "Choose your startup workspace."

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= theme.scaled(360) ? 2 : 1
                                    columnSpacing: theme.space12
                                    rowSpacing: theme.space4
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: "Startup Workspace"
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        font.weight: Font.Medium
                                    }
                                    ToneComboBox {
                                        accessibleLabel: qsTr("Startup workspace")
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        model: appShell.workspaceNames
                                        currentIndex: appShell.startupWorkspace
                                        onActivated: appShell.startupWorkspace = currentIndex
                                    }
                                }

                                PreferenceToggleRow {
                                    title: "Reopen last workspace on launch"
                                    description: "When enabled, Looksy returns to the last workspace you were using instead of the startup workspace above."
                                    checked: appShell.reopenLastWorkspace
                                    onToggled: function (checked) {
                                        appShell.reopenLastWorkspace = checked;
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
                            spacing: theme.gapUnrelated

                            PreferenceSection {
                                title: "Active Display Target"
                                searchTerms: "color monitor ICC display profile"
                                subtitle: "Preview uses the active display’s profile and viewing measurements."

                                ColumnLayout {
                                    id: displayTargetStatusBody
                                    Layout.fillWidth: true
                                    spacing: theme.space6

                                    Label {
                                        Layout.fillWidth: true
                                        text: editorPreview.systemMonitorDisplayTarget.verified ? "Calibrated target" : "Reference defaults — not measured"
                                        color: editorPreview.systemMonitorDisplayTarget.verified ? theme.success : theme.warning
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeControl
                                        font.weight: Font.DemiBold
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Display: " + (editorPreview.systemMonitorDisplayIdentity.length > 0 ? editorPreview.systemMonitorDisplayIdentity : "No active display identity")
                                        color: theme.textMain
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.WrapAnywhere
                                    }

                                    Label {
                                        Layout.fillWidth: true
                                        text: "ICC: " + (editorPreview.systemMonitorProfileName.length > 0 ? editorPreview.systemMonitorProfileName : "Unavailable") + "  •  Source: " + editorPreview.systemMonitorDisplayTarget.provenance
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }
                            }

                            PreferenceSection {
                                title: "Display Calibration"
                                searchTerms: "color calibration luminance white black nits meter"
                                subtitle: "Measured values apply only to this display."

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
                                            numericEntry: true
                                            objectName: "displayPeakLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator {
                                                bottom: 1
                                                top: 10000
                                                decimals: 6
                                            }
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
                                            numericEntry: true
                                            objectName: "displayBlackLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator {
                                                bottom: 0
                                                top: 100
                                                decimals: 9
                                            }
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
                                            numericEntry: true
                                            objectName: "displayReferenceWhiteField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator {
                                                bottom: 1
                                                top: 10000
                                                decimals: 6
                                            }
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
                                            numericEntry: true
                                            objectName: "displaySurroundLuminanceField"
                                            Layout.fillWidth: true
                                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                                            validator: DoubleValidator {
                                                bottom: 0
                                                top: 10000
                                                decimals: 6
                                            }
                                            Accessible.name: "Surround luminance in nits"
                                        }
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    visible: root.displayCalibrationMessage.length > 0
                                    text: root.displayCalibrationMessage
                                    color: root.displayCalibrationMessage.startsWith("Could not") ? theme.error : theme.textMuted
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

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    ActionButton {
                                        objectName: "applyDisplayCalibrationButton"
                                        text: "Apply to Active Display"
                                        tone: "primary"
                                        enabled: editorPreview.systemMonitorDisplayIdentity.length > 0 && calibrationIdentityField.text.trim().length > 0 && peakLuminanceField.acceptableInput && blackLuminanceField.acceptableInput && referenceWhiteField.acceptableInput && surroundLuminanceField.acceptableInput
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
                            spacing: theme.gapUnrelated

                            PreferenceSection {
                                title: "Timeline Defaults"
                                searchTerms: "timeline snapping ripple trim editing"
                                subtitle: "Defaults for new timelines."

                                PreferenceToggleRow {
                                    title: "Enable snapping by default"
                                    description: "Snap trims, drags, and clip placement to nearby edit points and playhead positions."
                                    checked: appShell.defaultTimelineSnapping
                                    onToggled: function (checked) {
                                        appShell.defaultTimelineSnapping = checked;
                                    }
                                }

                                PreferenceToggleRow {
                                    title: "Enable ripple trim by default"
                                    description: "Use ripple-aware timeline trims by default when sequence edits support it."
                                    checked: appShell.defaultTimelineRipple
                                    onToggled: function (checked) {
                                        appShell.defaultTimelineRipple = checked;
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
                            spacing: theme.gapUnrelated

                            PreferenceSection {
                                title: "Photos & Import Backup"
                                searchTerms: "storage photos library originals import backup folders"
                                subtitle: "Choose where originals and verified backup copies are stored."

                                StorageCard {
                                    label: "Photo Library"
                                    description: "Managed imports are copied here. Leave in Place imports remain at their source."
                                    path: appShell.photoLibraryLocation
                                    openFn: function () {
                                        appShell.openPhotoLibraryFolder();
                                    }
                                    changeFn: function () {
                                        root.choosePhotoLibraryLocation();
                                    }
                                    changeEnabled: !appShell.photoLibraryMigrationRunning
                                }

                                StorageCard {
                                    label: "Originals Backup"
                                    description: appShell.originalsBackupEnabled ? "Each import is copied here and verified with SHA-256 before the catalog reports success." : "Optional. Choose a separate drive or folder to protect imported originals."
                                    path: appShell.originalsBackupLocation
                                    openFn: function () {
                                        appShell.openOriginalsBackupFolder();
                                    }
                                    changeFn: function () {
                                        root.chooseOriginalsBackupLocation();
                                    }
                                    disableFn: appShell.originalsBackupEnabled ? function () {
                                        appShell.disableOriginalsBackup();
                                    } : null
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
                                title: "Catalog backups"
                                searchTerms: "storage catalog backups folders"
                                subtitle: "Verified snapshots of edits, metadata, and projects."
                                StorageCard {
                                    label: "Catalog Backups"
                                    description: "Checked database snapshots for projects, metadata, edits, and recovery. This is separate from the originals backup above."
                                    path: appShell.backupLocation
                                    openFn: function () {
                                        appShell.openBackupFolder();
                                    }
                                    changeFn: function () {
                                        root.chooseCatalogBackupLocation();
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
                            spacing: theme.gapUnrelated

                            PreferenceSection {
                                title: "Private by default"
                                searchTerms: "privacy crash reports diagnostics history support logs"
                                subtitle: "Photos and catalogs stay on this computer. You choose whether to send crash reports or share local diagnostic logs."

                                PreferenceToggleRow {
                                    title: "Send crash reports"
                                    description: typeof crashReporting !== "undefined" ? crashReporting.status : "Automatic crash reporting is not available in this version."
                                    checked: typeof crashReporting !== "undefined" && crashReporting.enabled
                                    enabled: typeof crashReporting !== "undefined" && crashReporting.available
                                    onToggled: function (checked) {
                                        crashReporting.enabled = checked;
                                    }
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
                                    description: appShell.localDiagnosticsEnabled ? "Enabled for this build. The latest sessions are retained locally for troubleshooting." : "Disabled for this build."
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
                                searchTerms: "privacy terms policies help support"
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

                                    Item {
                                        Layout.fillWidth: true
                                    }
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

                    ScrollView {
                        objectName: "preferencesSystemDetailsPage"
                        clip: true
                        contentWidth: availableWidth
                        ColumnLayout {
                            width: Math.max(0, parent.width)
                            spacing: theme.gapPanel
                            PreferenceSection {
                                title: "Preview Backend"
                                searchTerms: "system GPU CPU Metal acceleration preview backend"
                                subtitle: "Choose preview acceleration. CPU fallback remains available."

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: width >= theme.scaled(360) ? 2 : 1
                                    columnSpacing: theme.space12
                                    rowSpacing: theme.space4
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        text: "Preferred backend"
                                        color: theme.textStrong
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeBody
                                        font.weight: Font.Medium
                                    }
                                    ToneComboBox {
                                        accessibleLabel: qsTr("Preview backend preference")
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        model: previewBackend.backendPreferenceOptions
                                        currentIndex: Math.max(0, model.indexOf(previewBackend.preferredBackend))
                                        onActivated: previewBackend.preferredBackend = currentText
                                    }
                                }

                                ColumnLayout {
                                    id: backendStatusBody
                                    Layout.fillWidth: true
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
                                        text: "Metal available: " + (previewBackend.metalAvailable ? "Yes" : "No") + "  •  CUDA available: " + (previewBackend.cudaAvailable ? "Yes" : "No")
                                        color: theme.textFaint
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeCaption
                                        wrapMode: Text.WordWrap
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.space10

                                    Label {
                                        Layout.fillWidth: true
                                        text: previewBackend.fallbackReason.length > 0 ? previewBackend.fallbackReason : "Auto prefers Metal on Apple Silicon and CUDA on supported Windows NVIDIA systems."
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
                                title: "Runtime folders"
                                searchTerms: "system details runtime application data cache logs folders"
                                subtitle: "Local application data and troubleshooting files."

                                StorageCard {
                                    label: "Application Data"
                                    path: appShell.appDataLocation
                                    openFn: function () {
                                        appShell.openAppDataFolder();
                                    }
                                }

                                StorageCard {
                                    label: "Cache"
                                    path: appShell.cacheLocation
                                    openFn: function () {
                                        appShell.openCacheFolder();
                                    }
                                }

                                StorageCard {
                                    label: "Logs"
                                    path: appShell.logLocation
                                    openFn: function () {
                                        appShell.openLogFolder();
                                    }
                                }
                            }
                        }
                    }
                    ScrollView {
                        objectName: "preferencesModelsPage"
                        clip: true
                        contentWidth: availableWidth
                        AiModelsSettings {
                            width: Math.max(0, parent.width)
                            searchSetup: typeof searchModelSetup !== "undefined" ? searchModelSetup : null
                            agent: typeof agentEdit !== "undefined" ? agentEdit : null
                            preview: typeof editorPreview !== "undefined" ? editorPreview : null
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
                            text: root.currentPage === 1 ? "Only measured display values are labeled calibrated." : root.currentPage === 3 ? "Original and catalog backups use separate folders." : root.currentPage === 4 ? "Crash reports are optional. Review support bundles before sharing them." : "Saved automatically."
                            color: theme.textFaint
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.WordWrap
                        }

                        MenuButton {
                            objectName: "preferencesResetMenu"
                            visible: root.currentPage === 0 || root.currentPage === 2
                            text: "Reset…"
                            compact: true
                            primaryOpensMenu: true
                            tooltipText: "Reset settings for this page"
                            menuItems: root.currentPage === 2 ? [
                                {
                                    key: "timeline",
                                    label: "Reset Timeline Workspace",
                                    danger: true
                                }
                            ] : [
                                {
                                    key: "general",
                                    label: "Restore Defaults",
                                    danger: true,
                                    tooltip: "Restore startup, workspace behavior, and timeline defaults."
                                }
                            ]
                            onMenuItemTriggered: function (key) {
                                if (key === "timeline")
                                    appShell.resetTimelineWorkspaceSettings();
                                else if (key === "general")
                                    appShell.restoreDefaults();
                            }
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
