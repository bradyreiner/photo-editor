pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import PhotoEditor
import "../components"

Rectangle {
    id: root
    objectName: "timelineWorkspace"
    color: "transparent"

    Theme { id: theme }

    readonly property bool timelineSetupOnly: true
    readonly property bool fullScreenProgramActive: false
    readonly property string timelineVisibilityContract: "timeline-visibility:v1|always:header-program-monitor-timeline|contextual:source-panel-effects-panel-selected-clip-markers|one-expansion:tools-media-picker-effects-transitions-render-jobs|hover-focus:clip-actions-marker-actions-track-resize|diagnostic:render-errors-job-detail|preserve:functionality"
    readonly property var resolutionPresetLabels: [
        "1080 x 1350",
        "1920 x 1080",
        "3840 x 2160",
        "1080 x 1920",
        "1080 x 1080",
        "Custom"
    ]

    property string setupTimelineName: ""
    property string setupProjectBinding: ""
    property string setupResolutionPreset: "1080 x 1350"
    property string setupWidth: "1080"
    property string setupHeight: "1350"
    property string setupFrameRate: "30"
    property string setupAudioSampleRate: "48000"
    property string setupColorSpace: "Rec.709"
    property string setupDataLevels: "Legal"
    property string setupExportContainer: "mp4"
    property string setupExportCodec: "h264"
    property string setupExportPreset: "instagram"
    property bool setupCreatorTrackLayout: false
    property var recentTimelineRecords: []
    property bool recentTimelinesLoading: false
    property string pendingRecoveryCandidateId: ""
    property string recoveryOpenErrorMessage: ""

    function applyResolutionPreset() {
        switch (setupResolutionPreset) {
        case "1920 x 1080":
            setupWidth = "1920"
            setupHeight = "1080"
            break
        case "3840 x 2160":
            setupWidth = "3840"
            setupHeight = "2160"
            break
        case "1080 x 1920":
            setupWidth = "1080"
            setupHeight = "1920"
            break
        case "1080 x 1080":
            setupWidth = "1080"
            setupHeight = "1080"
            break
        case "1080 x 1350":
            setupWidth = "1080"
            setupHeight = "1350"
            break
        default:
            break
        }
    }

    function applySetupExportPreset(preset) {
        setupExportPreset = preset
        setupExportContainer = "mp4"
        setupExportCodec = "h264"
        if (preset === "youtube")
            setupResolutionPreset = "1920 x 1080"
        else if (preset === "instagram")
            setupResolutionPreset = "1080 x 1350"
        else
            setupResolutionPreset = "1080 x 1920"
        applyResolutionPreset()
    }

    function beginTimelineSetup() {
        setupTimelineName = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                ? catalogAssets.projectFilter + " Sequence"
                : "Untitled Timeline"
        setupProjectBinding = catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                ? catalogAssets.projectFilter
                : ""
        setupResolutionPreset = "1080 x 1350"
        setupWidth = "1080"
        setupHeight = "1350"
        setupFrameRate = "30"
        setupAudioSampleRate = "48000"
        setupColorSpace = "Rec.709"
        setupDataLevels = "Legal"
        setupExportContainer = "mp4"
        setupExportCodec = "h264"
        setupExportPreset = "instagram"
        setupCreatorTrackLayout = false
        recentTimelineRecords = timelineDocument.recentTimelineRecords
        recentTimelinesLoading = timelineDocument.recentTimelinesLoading
        timelineDocument.refreshRecentTimelines()
    }

    function createTimelineFromSetup() {
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
        })
    }

    Component.onCompleted: beginTimelineSetup()
    onVisibleChanged: {
        if (visible)
            timelineDocument.refreshRecentTimelines()
    }

    Connections {
        target: timelineDocument
        function onRecoveryOpenCompleted(candidateId, succeeded) {
            if (String(candidateId || "") !== root.pendingRecoveryCandidateId)
                return
            root.pendingRecoveryCandidateId = ""
            if (succeeded)
                root.recoveryOpenErrorMessage = ""
            else
                root.recoveryOpenErrorMessage = String(timelineDocument.status ||
                                                       "Recovery could not be opened. Your current timeline remains open.")
        }

        function onRecentTimelinesChanged() {
            root.recentTimelineRecords = timelineDocument.recentTimelineRecords
            root.recentTimelinesLoading = timelineDocument.recentTimelinesLoading
        }
    }

    ColumnLayout {
        objectName: "timelineSetupSurface"
        anchors.fill: parent
        visible: !timelineDocument.hasActiveTimeline
        spacing: 0

        Item { Layout.fillHeight: true }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            Layout.maximumWidth: 940
            Layout.leftMargin: theme.space28
            Layout.rightMargin: theme.space28
            spacing: theme.gapUnrelated

            GlassSurface {
                Layout.preferredWidth: 480
                Layout.minimumWidth: 340
                Layout.fillWidth: true
                implicitHeight: newTimelineColumn.implicitHeight + theme.panelPaddingStandard * 2
                radius: theme.radiusPanel
                elevated: true

                ColumnLayout {
                    id: newTimelineColumn
                    anchors.fill: parent
                    anchors.margins: theme.panelPaddingStandard
                    spacing: theme.space12

                    SectionHeader {
                        title: "New Timeline"
                        accent: theme.interaction400
                    }

                    ToneTextField {
                        Layout.fillWidth: true
                        placeholderText: "Timeline name"
                        text: root.setupTimelineName
                        onTextChanged: root.setupTimelineName = text
                    }

                    ToneTextField {
                        Layout.fillWidth: true
                        placeholderText: "Project binding"
                        text: root.setupProjectBinding
                        onTextChanged: root.setupProjectBinding = text
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        ToneComboBox {
                            accessibleLabel: qsTr("Timeline resolution preset")
                            Layout.fillWidth: true
                            model: root.resolutionPresetLabels
                            currentIndex: Math.max(0, root.resolutionPresetLabels.indexOf(root.setupResolutionPreset))
                            onActivated: {
                                root.setupResolutionPreset = root.resolutionPresetLabels[currentIndex]
                                root.applyResolutionPreset()
                            }
                        }

                        ToneTextField {
                            Layout.preferredWidth: 80
                            text: root.setupWidth
                            onTextChanged: root.setupWidth = text
                        }

                        Label {
                            text: "x"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeBody
                        }

                        ToneTextField {
                            Layout.preferredWidth: 80
                            text: root.setupHeight
                            onTextChanged: root.setupHeight = text
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            text: "FPS"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Timeline frame rate")
                            Layout.fillWidth: true
                            model: ["23.976", "24", "25", "29.97", "30", "60"]
                            currentIndex: Math.max(0, model.indexOf(root.setupFrameRate))
                            onActivated: root.setupFrameRate = model[currentIndex]
                        }

                        Label {
                            text: "Audio"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Audio sample rate")
                            Layout.fillWidth: true
                            model: ["44100", "48000", "96000"]
                            currentIndex: Math.max(0, model.indexOf(root.setupAudioSampleRate))
                            onActivated: root.setupAudioSampleRate = model[currentIndex]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            text: "Color"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Timeline color space")
                            Layout.fillWidth: true
                            model: ["Rec.709", "Display P3", "sRGB"]
                            currentIndex: Math.max(0, model.indexOf(root.setupColorSpace))
                            onActivated: root.setupColorSpace = model[currentIndex]
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Video data range")
                            Layout.preferredWidth: 100
                            model: ["Legal", "Full"]
                            currentIndex: Math.max(0, model.indexOf(root.setupDataLevels))
                            onActivated: root.setupDataLevels = model[currentIndex]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            text: "Export"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Export container")
                            Layout.fillWidth: true
                            model: ["mp4", "mov"]
                            currentIndex: Math.max(0, model.indexOf(root.setupExportContainer))
                            onActivated: root.setupExportContainer = model[currentIndex]
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Export codec")
                            Layout.fillWidth: true
                            model: ["h264", "prores"]
                            currentIndex: Math.max(0, model.indexOf(root.setupExportCodec))
                            onActivated: root.setupExportCodec = model[currentIndex]
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            text: "Preset"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ToneComboBox {
                            accessibleLabel: qsTr("Export preset")
                            Layout.fillWidth: true
                            model: ["youtube", "youtube_shorts", "tiktok", "instagram"]
                            currentIndex: Math.max(0, model.indexOf(root.setupExportPreset))
                            onActivated: root.applySetupExportPreset(model[currentIndex])
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: theme.gapDense

                        Label {
                            text: "Tracks"
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: theme.typeLabel
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Basic"
                            accessibleName: "Basic starter track layout"
                            tooltipText: "Start with the standard V1 video and A1 audio tracks."
                            checkable: true
                            checked: !root.setupCreatorTrackLayout
                            active: checked
                            onClicked: root.setupCreatorTrackLayout = false
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            objectName: "timelineCreatorTrackLayoutButton"
                            text: "Creator"
                            accessibleName: "Creator starter track layout"
                            tooltipText: "Adds B-roll, graphics, captions, voiceover, music, and SFX tracks in Video, Visual, and Audio groups."
                            checkable: true
                            checked: root.setupCreatorTrackLayout
                            active: checked
                            onClicked: root.setupCreatorTrackLayout = true
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: theme.space4
                        spacing: theme.gapRelated

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Use Active Project"
                            enabled: catalogAssets.projectFilter !== "all" && catalogAssets.projectFilter !== "unassigned"
                            disabledReason: "Choose an active project before binding this timeline."
                            onClicked: root.setupProjectBinding = catalogAssets.projectFilter
                        }

                        ActionButton {
                            Layout.fillWidth: true
                            text: "Create Timeline"
                            tone: "primary"
                            onClicked: root.createTimelineFromSetup()
                        }
                    }
                }
            }

            GlassSurface {
                Layout.preferredWidth: 340
                Layout.minimumWidth: 260
                Layout.fillWidth: true
                implicitHeight: recentColumn.implicitHeight + theme.panelPaddingStandard * 2
                Layout.alignment: Qt.AlignTop
                radius: theme.radiusPanel
                elevated: true

                ColumnLayout {
                    id: recentColumn
                    anchors.fill: parent
                    anchors.margins: theme.panelPaddingStandard
                    spacing: theme.space10

                    SectionHeader {
                        title: "Recent Timelines"
                        accent: theme.accentCool400
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.pendingRecoveryCandidateId.length > 0
                                 || root.recoveryOpenErrorMessage.length > 0

                        Label {
                            Layout.fillWidth: true
                            text: root.pendingRecoveryCandidateId.length > 0
                                  ? String(timelineDocument.status ||
                                           "Verifying the recovery before opening it…")
                                  : root.recoveryOpenErrorMessage
                            color: root.pendingRecoveryCandidateId.length > 0
                                   ? theme.textMuted : theme.warning
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeCaption
                            wrapMode: Text.Wrap
                        }

                        ActionButton {
                            visible: root.pendingRecoveryCandidateId.length > 0
                            text: "Cancel"
                            compact: true
                            onClicked: timelineDocument.cancelRecoveryOpen()
                        }
                    }

                    ListView {
                        id: recentTimelinesList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(320, Math.max(48,
                                                   count * 60 + Math.max(0, count - 1) * spacing))
                        clip: true
                        model: root.recentTimelineRecords
                        spacing: theme.space4
                        ScrollBar.vertical: ScrollBar {
                            policy: ScrollBar.AsNeeded
                        }

                        delegate: RecentDocumentRow {
                            required property var modelData
                            required property int index
                            objectName: "timelineRecentDocumentRow" + index

                            width: recentTimelinesList.width
                            height: 60
                            iconName: "timeline"
                            title: modelData.name || "Untitled"
                            detail: (modelData.isRecovery ? "Recoverable work · " : "Saved · ")
                                    + (modelData.updatedAt || modelData.createdAt || "")
                            accent: theme.accentCool400
                            actionable: !timelineDocument.recoveryOpenInProgress
                                        && Boolean(modelData.recoveryCandidateId || modelData.documentPath)
                            onActivated: {
                                if (modelData.recoveryCandidateId) {
                                    root.recoveryOpenErrorMessage = ""
                                    root.pendingRecoveryCandidateId = String(modelData.recoveryCandidateId)
                                    if (!timelineDocument.openRecoveryCandidate(modelData.recoveryCandidateId)) {
                                        root.pendingRecoveryCandidateId = ""
                                        root.recoveryOpenErrorMessage = String(timelineDocument.status ||
                                                                               "Recovery could not be opened. Your current timeline remains open.")
                                    }
                                } else if (modelData.documentPath && modelData.documentPath.length > 0) {
                                    timelineDocument.loadTimeline(modelData.documentPath)
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        visible: recentTimelinesList.count === 0
                        text: root.recentTimelinesLoading
                              ? "Loading recent timelines…"
                              : "No saved timelines yet."
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeBody
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }

}
