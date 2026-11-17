pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var workspace
    spacing: theme.space4
    readonly property var savedLayout: workspace.savedBoardPhotoLayoutSettings()
    readonly property var selectedPhoto: workspace.currentPhotoLayerData()
    readonly property var framing: selectedPhoto.boardPhotoFraming || ({horizontal: 0.5, vertical: 0.5, zoom: 1})
    property bool controlsReady: false
    property bool reframing: false
    Component.onCompleted: { controlsReady = true; restoreLayoutControls() }
    onSavedLayoutChanged: restoreLayoutControls()
    function restoreLayoutControls() {
        if (!controlsReady) return
        preset.currentIndex = Math.max(0, ["stack", "columns", "grid", "fit", "fill"].indexOf(savedLayout.preset))
        margin.value = Number(savedLayout.margin || 0)
        spacing.value = Number(savedLayout.spacing || 0)
        border.value = Number(savedLayout.border || 0)
    }
    onFramingChanged: {
        if (!controlsReady) return
        horizontal.value = Math.round(framing.horizontal * 100)
        vertical.value = Math.round(framing.vertical * 100)
        zoom.value = Math.round(framing.zoom * 100)
    }
    function applyFraming() {
        root.workspace.editorPreviewController.setBoardPhotoFraming(root.workspace.normalizedActivePhotoLayerIndex(),
            horizontal.value / 100, vertical.value / 100, zoom.value / 100)
    }
    Theme { id: theme }
    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space4
        ActionButton {
            objectName: "boardCanvasSizeButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Canvas size…")
            iconName: "geometry"
            showLeadingIcon: true
            tone: "secondary"
            compact: true
            enabled: root.workspace.editBoardModel.length === 1 && !root.workspace.editBoardHasPhotoOrigin
            disabledReason: qsTr("Canvas size is available for single-board canvas documents.")
            onClicked: root.workspace.boardCanvasSizeRequested()
        }
        ActionButton {
            objectName: "boardAddImagesButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Add media")
            iconName: "image"
            showLeadingIcon: true
            tone: "secondary"
            compact: true
            tooltipText: qsTr("Choose photos or videos in Library using search and filters.")
            onClicked: root.workspace.openBoardImagePicker()
        }
    }
    ActionButton {
        objectName: "boardEditVideoSequenceButton"
        Layout.fillWidth: true
        text: root.workspace.boardVideoSequence.videoPath ? qsTr("Edit video sequence") : qsTr("Open board in Timeline")
        iconName: "timeline"
        showLeadingIcon: true
        tone: "secondary"
        compact: true
        onClicked: { root.workspace.boardSequencePlaying = false; root.workspace.boardVideoSequenceRequested() }
    }
    ActionButton {
        objectName: "boardSequencePlayButton"
        visible: root.workspace.boardHasVideoMedia
        Layout.fillWidth: true
        text: root.workspace.boardVideoSequence.needsUpdate || !root.workspace.boardVideoSequence.videoPath
            ? qsTr("Preview current layout")
            : root.workspace.boardSequencePlaying ? qsTr("Pause sequence") : qsTr("Play sequence")
        iconName: root.workspace.boardSequencePlaying ? "pause" : "play"
        showLeadingIcon: true
        tone: "secondary"
        compact: true
        tooltipText: root.workspace.boardVideoSequence.needsUpdate
            ? qsTr("Play the current layout in Timeline without rendering an export.")
            : qsTr("Play or pause the saved video sequence.")
        onClicked: root.workspace.toggleBoardVideoPlayback()
    }
    ActionButton {
        objectName: "boardSequenceBackgroundButton"
        visible: root.workspace.boardHasVideoMedia
        Layout.fillWidth: true
        text: root.workspace.editSequenceBackgroundMode ? qsTr("Show video preview") : qsTr("Edit layout")
        iconName: root.workspace.editSequenceBackgroundMode ? "video" : "geometry"
        showLeadingIcon: true
        tone: "secondary"
        compact: true
        enabled: !root.workspace.editSequenceBackgroundMode || !root.workspace.boardVideoSequence.needsUpdate
        disabledReason: qsTr("Apply the updated layout in Timeline before showing the sequence.")
        onClicked: {
            root.workspace.boardSequencePlaying = false
            root.workspace.editSequenceBackgroundMode = !root.workspace.editSequenceBackgroundMode
        }
    }
    RowLayout {
        visible: root.workspace.boardSequencePreviewActive
        Layout.fillWidth: true
        spacing: theme.space4
        ActionButton {
            objectName: "boardSequenceRestartButton"
            text: qsTr("Restart")
            iconName: "refresh"
            showLeadingIcon: true
            tone: "secondary"
            compact: true
            onClicked: root.workspace.boardSequenceSeekRequested(0)
        }
        Slider {
            objectName: "boardSequenceSeekSlider"
            Layout.fillWidth: true
            from: 0
            to: Math.max(1, root.workspace.boardSequenceDurationMs)
            value: root.workspace.boardSequencePositionMs
            enabled: root.workspace.boardSequenceDurationMs > 0
            Accessible.name: qsTr("Video sequence position")
            onMoved: root.workspace.boardSequenceSeekRequested(value)
        }
        Label {
            text: (root.workspace.boardSequencePositionMs / 1000).toFixed(1) + " / " + (root.workspace.boardSequenceDurationMs / 1000).toFixed(1) + " s"
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
        }
    }
    Label {
        Layout.fillWidth: true
        visible: !!root.workspace.boardVideoSequence.needsUpdate
        text: qsTr("Preview the current layout in Timeline. Apply to board when you're ready to update the saved video and export.")
        color: theme.warning
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
    ActionButton {
        objectName: "boardSequenceExportButton"
        Layout.fillWidth: true
        visible: !!root.workspace.boardVideoSequence.videoPath
        text: qsTr("Export carousel…")
        iconName: "export"
        showLeadingIcon: true
        tone: "secondary"
        compact: true
        enabled: !root.workspace.editBoardExportBusy && !root.workspace.boardVideoSequence.needsUpdate
        disabledReason: qsTr("Apply the updated sequence before exporting.")
        onClicked: root.workspace.boardSequenceExportRequested()
    }
    StudioRowRule { Layout.fillWidth: true }
    ToneComboBox {
        id: preset
        objectName: "boardPhotoLayoutPreset"
        Layout.fillWidth: true
        visible: !root.workspace.boardSequencePreviewActive
        accessibleLabel: qsTr("Photo and video layout")
        model: [qsTr("Vertical stack"), qsTr("Side by side"), qsTr("Grid"), qsTr("Full photo"), qsTr("Fill board")]
    }
    GridLayout {
        Layout.fillWidth: true
        visible: !root.workspace.boardSequencePreviewActive
        columns: 2
        rowSpacing: theme.space3
        columnSpacing: theme.space6
        Label { text: qsTr("Outer margin (px)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: margin; objectName: "boardPhotoMargin"; Layout.fillWidth: true; from: 0; to: 512; value: 0; editable: true; accessibleLabel: qsTr("Extra outer white margin in pixels") }
        Label { text: qsTr("Photo spacing (px)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: spacing; objectName: "boardPhotoSpacing"; Layout.fillWidth: true; from: 0; to: 512; value: 0; editable: true; accessibleLabel: qsTr("Extra white spacing in pixels") }
        Label { text: qsTr("Border width (px)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: border; objectName: "boardPhotoBorder"; Layout.fillWidth: true; from: 0; to: 64; value: 4; editable: true; accessibleLabel: qsTr("White border width in pixels") }
    }
    RowLayout {
        visible: !root.workspace.boardSequencePreviewActive
        Layout.fillWidth: true
        spacing: theme.space4
        ActionButton {
            objectName: "boardApplyPhotoLayoutButton"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: qsTr("Apply layout")
            iconName: "check"
            showLeadingIcon: true
            tone: "primary"
            compact: true
            enabled: root.workspace.boardPhotoLayoutTargets().length >= (preset.currentIndex < 3 ? 2 : 1)
            disabledReason: qsTr("Add photos or videos to arrange them on this board.")
            onClicked: {
                margin.commitTextEntry(); spacing.commitTextEntry(); border.commitTextEntry()
                root.workspace.applyBasicBoardPhotoLayout(["stack", "columns", "grid", "fit", "fill"][preset.currentIndex], {
                    margin: margin.value, spacing: spacing.value, border: border.value
                })
            }
        }
        ActionButton {
            objectName: "boardPhotoReframeButton"
            visible: root.selectedPhoto.kind === "source" && !!root.selectedPhoto.boardPhotoCrop
            Layout.preferredWidth: theme.controlHeightDense
            Layout.preferredHeight: theme.controlHeightDense
            showText: false
            iconName: "crop"
            tone: "secondary"
            compact: true
            accessibleName: root.reframing ? qsTr("Hide framing controls") : qsTr("Reframe selected media")
            tooltipText: root.reframing ? qsTr("Hide framing controls") : qsTr("Reframe selected media")
            onClicked: root.reframing = !root.reframing
        }
    }
    GridLayout {
        Layout.fillWidth: true
        visible: !root.workspace.boardSequencePreviewActive && root.selectedPhoto.kind === "source" &&
                 !!root.selectedPhoto.boardPhotoCrop && root.reframing
        columns: 2
        rowSpacing: theme.space3
        columnSpacing: theme.space6
        Label { Layout.columnSpan: 2; text: qsTr("Reframe selected media"); color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: theme.typeCaption; font.weight: theme.weightSection }
        Label { text: qsTr("Horizontal (%)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: horizontal; objectName: "boardPhotoReframeHorizontal"; Layout.fillWidth: true; from: 0; to: 100; value: Math.round(root.framing.horizontal * 100); editable: true; accessibleLabel: qsTr("Photo horizontal framing"); onValueModified: root.applyFraming() }
        Label { text: qsTr("Vertical (%)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: vertical; objectName: "boardPhotoReframeVertical"; Layout.fillWidth: true; from: 0; to: 100; value: Math.round(root.framing.vertical * 100); editable: true; accessibleLabel: qsTr("Photo vertical framing"); onValueModified: root.applyFraming() }
        Label { text: qsTr("Zoom (%)"); color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
        ToneSpinBox { id: zoom; objectName: "boardPhotoReframeZoom"; Layout.fillWidth: true; from: 100; to: 800; value: Math.round(root.framing.zoom * 100); editable: true; accessibleLabel: qsTr("Photo framing zoom"); onValueModified: root.applyFraming() }
    }
    Label {
        Layout.fillWidth: true
        text: root.workspace.boardSequencePreviewActive
            ? qsTr("Choose Edit layout to arrange and resize photos and videos together. Trim and position clips in time in Timeline.")
            : qsTr("Arrange photo and video frames here. Text layers stay independent; trim video in Timeline.")
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: theme.typeCaption
        wrapMode: Text.Wrap
    }
}
