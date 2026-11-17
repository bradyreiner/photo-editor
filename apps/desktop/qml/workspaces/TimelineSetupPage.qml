pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root

    readonly property bool fullScreenProgramActive: false
    property string pendingRecoveryCandidateId: ""
    property var recentTimelineRecords: []
    property bool recentTimelinesLoading: false
    property string recoveryOpenErrorMessage: ""
    readonly property var resolutionPresetOptions: [
        {label: "Portrait · 4:5", value: "1080 x 1350"},
        {label: "Landscape · 16:9", value: "1920 x 1080"},
        {label: "4K · 16:9", value: "3840 x 2160"},
        {label: "Portrait · 9:16", value: "1080 x 1920"},
        {label: "Square · 1:1", value: "1080 x 1080"},
        {label: "Custom", value: "Custom"}
    ]
    readonly property var resolutionPresetLabels: resolutionPresetOptions.map(option => option.value)
    readonly property var exportPresetOptions: [
        {label: "YouTube", value: "youtube"},
        {label: "YouTube Shorts", value: "youtube_shorts"},
        {label: "TikTok", value: "tiktok"},
        {label: "Instagram", value: "instagram"}
    ]
    property bool setupAdvancedExpanded: false
    property string setupAudioSampleRate: "48000"
    property string setupColorSpace: "Rec.709"
    property bool setupCreatorTrackLayout: false
    property string setupDataLevels: "Legal"
    property bool setupExportDetailsExpanded: false
    property string setupExportCodec: "h264"
    property string setupExportContainer: "mp4"
    property string setupExportPreset: "instagram"
    property string setupFrameRate: "30"
    property string setupHeight: "1350"
    property bool setupImageDetailsExpanded: true
    property string setupProjectBinding: ""
    property bool setupProjectDetailsExpanded: false
    property string setupResolutionPreset: "1080 x 1350"
    property string setupTimelineName: ""
    readonly property bool setupValid: setupTimelineName.trim().length > 0 && isFinite(Number(setupWidth)) && Number(setupWidth) >= 16 && Math.floor(Number(setupWidth)) === Number(setupWidth) && isFinite(Number(setupHeight)) && Number(setupHeight) >= 16 && Math.floor(Number(setupHeight)) === Number(setupHeight) && isFinite(Number(setupFrameRate)) && Number(setupFrameRate) >= 1 && isFinite(Number(setupAudioSampleRate)) && Number(setupAudioSampleRate) >= 8000
    property string setupWidth: "1080"
    readonly property bool timelineSetupOnly: true
    readonly property string timelineVisibilityContract: "timeline-visibility:v1|always:header-program-monitor-timeline|contextual:source-panel-effects-panel-selected-clip-markers|one-expansion:tools-media-picker-effects-transitions-render-jobs|hover-focus:clip-actions-marker-actions-track-resize|diagnostic:render-errors-job-detail|preserve:functionality"

    function applyResolutionPreset() {
        switch (setupResolutionPreset) {
        case "1920 x 1080":
            setupWidth = "1920";
            setupHeight = "1080";
            break;
        case "3840 x 2160":
            setupWidth = "3840";
            setupHeight = "2160";
            break;
        case "1080 x 1920":
            setupWidth = "1080";
            setupHeight = "1920";
            break;
        case "1080 x 1080":
            setupWidth = "1080";
            setupHeight = "1080";
            break;
        case "1080 x 1350":
            setupWidth = "1080";
            setupHeight = "1350";
            break;
        default:
            break;
        }
    }
    function applySetupExportPreset(preset) {
        setupExportPreset = preset;
        setupExportContainer = "mp4";
        setupExportCodec = "h264";
        if (preset === "youtube")
            setupResolutionPreset = "1920 x 1080";
        else if (preset === "instagram")
            setupResolutionPreset = "1080 x 1350";
        else
            setupResolutionPreset = "1080 x 1920";
        applyResolutionPreset();
    }
    function beginTimelineSetup() {
        setupTimelineName = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned" ? catalogAssets.projectFilter + " Sequence" : "Untitled Timeline";
        setupProjectBinding = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned" ? catalogAssets.projectFilter : "";
        setupResolutionPreset = "1080 x 1350";
        setupWidth = "1080";
        setupHeight = "1350";
        setupFrameRate = "30";
        setupAudioSampleRate = "48000";
        setupColorSpace = "Rec.709";
        setupDataLevels = "Legal";
        setupExportContainer = "mp4";
        setupExportCodec = "h264";
        setupExportPreset = "instagram";
        setupCreatorTrackLayout = false;
        setupProjectDetailsExpanded = false;
        setupImageDetailsExpanded = true;
        setupExportDetailsExpanded = false;
        recentTimelineRecords = timelineDocument.recentTimelineRecords;
        recentTimelinesLoading = timelineDocument.recentTimelinesLoading;
        timelineDocument.refreshRecentTimelines();
    }
    function createTimelineFromSetup() {
        if (!setupValid)
            return;
        timelineDocument.createTimelineWithSettings({
            "timelineName": setupTimelineName,
            "projectBinding": setupProjectBinding,
            "width": Math.max(16, Number(setupWidth || 1080)),
            "height": Math.max(16, Number(setupHeight || 1350)),
            "frameRate": Math.max(1, Number(setupFrameRate || 30)),
            "audioSampleRate": Math.max(8000, Number(setupAudioSampleRate || 48000)),
            "colorSpace": setupColorSpace,
            "dataLevels": setupDataLevels,
            "exportContainer": setupExportContainer,
            "exportCodec": setupExportCodec,
            "exportPreset": setupExportPreset,
            "trackLayout": setupCreatorTrackLayout ? "creator_social" : "standard"
        });
    }

    color: "transparent"
    objectName: "timelineWorkspace"

    Component.onCompleted: beginTimelineSetup()
    onVisibleChanged: {
        if (visible)
            timelineDocument.refreshRecentTimelines();
    }

    Theme {
        id: theme

    }
    Connections {
        function onRecentTimelinesChanged() {
            root.recentTimelineRecords = timelineDocument.recentTimelineRecords;
            root.recentTimelinesLoading = timelineDocument.recentTimelinesLoading;
        }
        function onRecoveryOpenCompleted(candidateId, succeeded) {
            if (String(candidateId || "") !== root.pendingRecoveryCandidateId)
                return;
            root.pendingRecoveryCandidateId = "";
            if (succeeded)
                root.recoveryOpenErrorMessage = "";
            else
                root.recoveryOpenErrorMessage = String(timelineDocument.status || "Recovery could not be opened. Your current timeline remains open.");
        }

        target: timelineDocument
    }
    ColumnLayout {
        anchors.fill: parent
        objectName: "timelineSetupSurface"
        spacing: theme.space12
        visible: !timelineDocument.hasActiveTimeline

        ScrollView {
            id: setupScroll

            Layout.fillHeight: true
            Layout.fillWidth: true
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            clip: true
            contentWidth: availableWidth
            objectName: "timelineSetupScroll"

            GridLayout {
                columnSpacing: theme.gapPanel
                columns: width >= theme.scaled(860) ? 2 : 1
                rowSpacing: theme.gapPanel
                width: Math.max(0, Math.min(setupScroll.availableWidth, theme.scaled(940)))
                x: Math.max(0, (setupScroll.availableWidth - width) / 2)

                GlassSurface {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: theme.scaled(480)
                    accentRail: false
                    elevated: false
                    implicitHeight: newTimelineColumn.implicitHeight + theme.panelPaddingStandard * 2

                    ColumnLayout {
                        id: newTimelineColumn

                        anchors.fill: parent
                        anchors.margins: theme.panelPaddingStandard
                        spacing: theme.gapControl

                        SectionHeader {
                            title: "New timeline"
                        }
                        ToneTextField {
                            Accessible.name: "Timeline name"
                            Layout.fillWidth: true
                            objectName: "timelineSetupNameField"
                            placeholderText: "Timeline name"
                            text: root.setupTimelineName

                            onTextChanged: root.setupTimelineName = text
                        }
                        ToneComboBox {
                            Layout.fillWidth: true
                            accessibleLabel: "Timeline format"
                            currentIndex: Math.max(0, root.resolutionPresetLabels.indexOf(root.setupResolutionPreset))
                            model: root.resolutionPresetOptions
                            textRole: "label"
                            objectName: "timelineSetupFormatCombo"

                            onActivated: {
                                root.setupResolutionPreset = root.resolutionPresetOptions[currentIndex].value;
                                root.applyResolutionPreset();
                                if (root.setupResolutionPreset === "Custom")
                                    root.setupAdvancedExpanded = true;
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: theme.gapControl

                            ActionButton {
                                active: root.setupAdvancedExpanded
                                compact: true
                                objectName: "timelineSetupAdvancedButton"
                                text: root.setupAdvancedExpanded ? "Hide advanced" : "Advanced settings"
                                onClicked: root.setupAdvancedExpanded = !root.setupAdvancedExpanded
                            }

                            Label {
                                Layout.fillWidth: true
                                visible: !root.setupAdvancedExpanded
                                text: root.setupFrameRate + " fps · " + root.setupAudioSampleRate + " Hz audio · " +
                                      root.setupColorSpace + " · " + root.setupExportPreset.replace("_", " ") +
                                      " · " + root.setupExportContainer + " / " + root.setupExportCodec +
                                      " · " + (root.setupCreatorTrackLayout ? "Creator tracks" : "Basic tracks")
                                color: theme.textMuted
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                wrapMode: Text.WordWrap
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            objectName: "timelineSetupAdvancedOptions"
                            spacing: theme.gapControl
                            visible: root.setupAdvancedExpanded

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Project and tracks"
                                helpText: "Choose the project that owns this timeline and how its starting tracks are arranged."
                                expandable: true
                                expanded: root.setupProjectDetailsExpanded
                                onToggled: root.setupProjectDetailsExpanded = expanded

                                ToneTextField {
                                    Accessible.name: "Project binding"
                                    Layout.fillWidth: true
                                    objectName: "timelineSetupProjectField"
                                    placeholderText: "Project (optional)"
                                    text: root.setupProjectBinding

                                    onTextChanged: root.setupProjectBinding = text
                                }
                                ActionButton {
                                    compact: true
                                    disabledReason: "Choose a project in Library first."
                                    enabled: catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                                    text: "Use active project"

                                    onClicked: root.setupProjectBinding = catalogAssets.projectFilter
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense

                                    Label {
                                        text: "Starter tracks"
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                    }
                                    ActionButton {
                                        Layout.fillWidth: true
                                        accessibleName: "Basic starter track layout"
                                        active: checked
                                        checkable: true
                                        checked: !root.setupCreatorTrackLayout
                                        text: "Basic"
                                        tooltipText: "Start with the standard V1 video and A1 audio tracks."
                                        onClicked: root.setupCreatorTrackLayout = false
                                    }
                                    ActionButton {
                                        Layout.fillWidth: true
                                        accessibleName: "Creator starter track layout"
                                        active: checked
                                        checkable: true
                                        checked: root.setupCreatorTrackLayout
                                        objectName: "timelineCreatorTrackLayoutButton"
                                        text: "Creator"
                                        tooltipText: "Adds B-roll, graphics, captions, voiceover, music, and SFX tracks in Video, Visual, and Audio groups."
                                        onClicked: root.setupCreatorTrackLayout = true
                                    }
                                }
                            }

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Picture and sound"
                                helpText: "Set the sequence frame, playback rate, audio rate, and color levels."
                                expandable: true
                                expanded: root.setupImageDetailsExpanded
                                onToggled: root.setupImageDetailsExpanded = expanded

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense

                                    Label {
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        font.pixelSize: theme.typeLabel
                                        text: "Size"
                                    }
                                    ToneTextField {
                                        Accessible.name: "Timeline width in pixels"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        objectName: "timelineSetupWidthField"
                                        text: root.setupWidth
                                        onTextEdited: {
                                            root.setupWidth = text
                                            root.setupResolutionPreset = "Custom"
                                        }
                                    }
                                    Label {
                                        color: theme.textMuted
                                        font.family: theme.fontUi
                                        text: "×"
                                    }
                                    ToneTextField {
                                        Accessible.name: "Timeline height in pixels"
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 0
                                        objectName: "timelineSetupHeightField"
                                        text: root.setupHeight
                                        onTextEdited: {
                                            root.setupHeight = text
                                            root.setupResolutionPreset = "Custom"
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense
                                    Label { color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeLabel; text: "FPS" }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Timeline frame rate")
                                        currentIndex: Math.max(0, model.indexOf(root.setupFrameRate))
                                        model: ["23.976", "24", "25", "29.97", "30", "60"]
                                        onActivated: root.setupFrameRate = model[currentIndex]
                                    }
                                    Label { color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeLabel; text: "Audio" }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Audio sample rate")
                                        currentIndex: Math.max(0, model.indexOf(root.setupAudioSampleRate))
                                        model: ["44100", "48000", "96000"]
                                        onActivated: root.setupAudioSampleRate = model[currentIndex]
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense
                                    Label { color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeLabel; text: "Color" }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Timeline color space")
                                        currentIndex: Math.max(0, model.indexOf(root.setupColorSpace))
                                        model: ["Rec.709", "Display P3", "sRGB"]
                                        onActivated: root.setupColorSpace = model[currentIndex]
                                    }
                                    ToneComboBox {
                                        Layout.preferredWidth: theme.scaled(100)
                                        accessibleLabel: qsTr("Video data range")
                                        currentIndex: Math.max(0, model.indexOf(root.setupDataLevels))
                                        model: ["Legal", "Full"]
                                        onActivated: root.setupDataLevels = model[currentIndex]
                                    }
                                }
                            }

                            InspectorSection {
                                Layout.fillWidth: true
                                title: "Export defaults"
                                helpText: "Choose a delivery preset and starting container and codec. You can change these before export."
                                expandable: true
                                expanded: root.setupExportDetailsExpanded
                                onToggled: root.setupExportDetailsExpanded = expanded

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeLabel; text: "Preset" }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Export preset")
                                        currentIndex: Math.max(0, root.exportPresetOptions.findIndex(option => option.value === root.setupExportPreset))
                                        model: root.exportPresetOptions
                                        textRole: "label"
                                        onActivated: root.applySetupExportPreset(root.exportPresetOptions[currentIndex].value)
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: theme.gapDense
                                    Label { color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeLabel; text: "Format" }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Export container")
                                        currentIndex: Math.max(0, model.indexOf(root.setupExportContainer))
                                        model: ["mp4", "mov"]
                                        onActivated: root.setupExportContainer = model[currentIndex]
                                    }
                                    ToneComboBox {
                                        Layout.fillWidth: true
                                        accessibleLabel: qsTr("Export codec")
                                        currentIndex: Math.max(0, model.indexOf(root.setupExportCodec))
                                        model: ["h264", "prores"]
                                        onActivated: root.setupExportCodec = model[currentIndex]
                                    }
                                }
                            }
                        }
                    }
                }
                GlassSurface {
                    Layout.alignment: Qt.AlignTop
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredWidth: theme.scaled(340)
                    elevated: false
                    implicitHeight: recentColumn.implicitHeight + theme.panelPaddingStandard * 2
                    radius: theme.radiusPanel

                    ColumnLayout {
                        id: recentColumn

                        anchors.fill: parent
                        anchors.margins: theme.panelPaddingStandard
                        spacing: theme.space10

                        SectionHeader {
                            accent: theme.accentCool400
                            title: "Recent Timelines"
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.pendingRecoveryCandidateId.length > 0 || root.recoveryOpenErrorMessage.length > 0

                            Label {
                                Layout.fillWidth: true
                                color: root.pendingRecoveryCandidateId.length > 0 ? theme.textMuted : theme.warning
                                font.family: theme.fontUi
                                font.pixelSize: theme.typeCaption
                                text: root.pendingRecoveryCandidateId.length > 0 ? String(timelineDocument.status || "Verifying the recovery before opening it…") : root.recoveryOpenErrorMessage
                                wrapMode: Text.Wrap
                            }
                            ActionButton {
                                compact: true
                                text: "Cancel"
                                visible: root.pendingRecoveryCandidateId.length > 0

                                onClicked: timelineDocument.cancelRecoveryOpen()
                            }
                        }
                        ListView {
                            id: recentTimelinesList

                            Layout.fillWidth: true
                            Layout.preferredHeight: Math.min(theme.scaled(320), Math.max(theme.scaled(48), count * theme.scaled(60) + Math.max(0, count - 1) * spacing))
                            clip: true
                            model: root.recentTimelineRecords
                            spacing: theme.space4

                            ScrollBar.vertical: ScrollBar {
                                policy: ScrollBar.AsNeeded
                            }
                            delegate: RecentDocumentRow {
                                required property int index
                                required property var modelData

                                accent: theme.accentCool400
                                actionable: !timelineDocument.recoveryOpenInProgress && Boolean(modelData.recoveryCandidateId || modelData.documentPath)
                                detail: (modelData.isRecovery ? "Recoverable work · " : "Saved · ") + (modelData.updatedAt || modelData.createdAt || "")
                                height: theme.scaled(60)
                                iconName: "timeline"
                                objectName: "timelineRecentDocumentRow" + index
                                title: modelData.name || "Untitled"
                                width: recentTimelinesList.width

                                onActivated: {
                                    if (modelData.recoveryCandidateId) {
                                        root.recoveryOpenErrorMessage = "";
                                        root.pendingRecoveryCandidateId = String(modelData.recoveryCandidateId);
                                        if (!timelineDocument.openRecoveryCandidate(modelData.recoveryCandidateId)) {
                                            root.pendingRecoveryCandidateId = "";
                                            root.recoveryOpenErrorMessage = String(timelineDocument.status || "Recovery could not be opened. Your current timeline remains open.");
                                        }
                                    } else if (modelData.documentPath && modelData.documentPath.length > 0) {
                                        timelineDocument.loadTimeline(modelData.documentPath);
                                    }
                                }
                            }
                        }
                        Label {
                            Layout.fillWidth: true
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeBody
                            horizontalAlignment: Text.AlignHCenter
                            text: root.recentTimelinesLoading ? "Loading recent timelines…" : "No saved timelines yet."
                            visible: recentTimelinesList.count === 0
                        }
                    }
                }
            }
        }
        RowLayout {
            Layout.bottomMargin: theme.panelPadding
            Layout.fillWidth: true
            Layout.leftMargin: theme.panelPadding
            Layout.rightMargin: theme.panelPadding
            spacing: theme.gapControl

            Label {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                text: root.setupWidth + " × " + root.setupHeight + " px · " + root.setupFrameRate + " fps"
                wrapMode: Text.Wrap
            }
            ActionButton {
                disabledReason: "Enter a name, valid dimensions, frame rate, and audio sample rate."
                enabled: root.setupValid
                objectName: "timelineCreateButton"
                text: "Create timeline"
                tone: "primary"

                onClicked: root.createTimelineFromSetup()
            }
        }
    }
}
