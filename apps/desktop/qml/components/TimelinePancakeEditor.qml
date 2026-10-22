pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: root

    required property var timelineDocument
    property var sourceRecords: []
    property var sourcePreview: ({})
    property string sourcePath: ""
    property var selectedClipIds: []
    property string draggingClipId: ""
    property real sourceInSeconds: 0
    property real sourceOutSeconds: 0
    property real destinationSeconds: 0
    property string editMode: "overwrite"
    property string transferMode: "copy"
    property var transferPreview: ({})
    readonly property real sourceDuration: Math.max(0.001, Number(sourcePreview.durationSeconds || 1))
    readonly property real destinationDuration: Math.max(0.001, Number(timelineDocument.totalDurationSeconds || 1))

    signal closeRequested()
    signal transferCommitted(int clipCount)

    Theme { id: theme }

    function sourceLabels() {
        let labels = []
        for (let index = 0; index < sourceRecords.length; ++index) {
            const record = sourceRecords[index]
            labels.push(String(record.name || "Untitled sequence") + "  ·  " +
                        root.timeText(Number(record.durationSeconds || 0)))
        }
        return labels
    }

    function timeText(seconds) {
        const value = Math.max(0, Number(seconds || 0))
        const minutes = Math.floor(value / 60)
        const remaining = value - minutes * 60
        return minutes + ":" + (remaining < 10 ? "0" : "") + remaining.toFixed(2)
    }

    function refreshSources(preferredPath) {
        sourceRecords = timelineDocument.pancakeSequences()
        if (sourceRecords.length === 0) {
            sourcePath = ""
            sourcePreview = ({})
            selectedClipIds = []
            updateTransferPreview()
            return
        }
        let index = 0
        const preferred = String(preferredPath || sourcePath || "")
        for (let candidate = 0; candidate < sourceRecords.length; ++candidate) {
            if (String(sourceRecords[candidate].documentPath || "") === preferred) {
                index = candidate
                break
            }
        }
        sourceSequenceCombo.currentIndex = index
        loadSourceAt(index)
    }

    function loadSourceAt(index) {
        if (index < 0 || index >= sourceRecords.length)
            return
        sourcePath = String(sourceRecords[index].documentPath || "")
        sourcePreview = timelineDocument.pancakeSequencePreview(sourcePath)
        selectedClipIds = []
        sourceInSeconds = 0
        sourceOutSeconds = Math.min(sourceDuration, Math.max(1, sourceDuration))
        updateTransferPreview()
    }

    function selectOnlyClip(clipId) {
        selectedClipIds = [String(clipId || "")]
        updateTransferPreview()
    }

    function updateTransferPreview() {
        if (sourcePath.length === 0) {
            transferPreview = ({})
            return
        }
        transferPreview = timelineDocument.pancakeTransferPreview(
                    sourcePath, selectedClipIds,
                    Number(sourceInSeconds), Number(sourceOutSeconds),
                    Number(destinationSeconds), editMode, transferMode)
    }

    function commitTransfer(clipIds) {
        const ids = clipIds || selectedClipIds
        const count = timelineDocument.transferFromPancakeSequence(
                    sourcePath, ids,
                    Number(sourceInSeconds), Number(sourceOutSeconds),
                    Number(destinationSeconds), editMode, transferMode)
        if (count > 0) {
            selectedClipIds = []
            sourcePreview = timelineDocument.pancakeSequencePreview(sourcePath)
            destinationSeconds = Number(timelineDocument.sequencePlayheadSeconds || destinationSeconds)
            updateTransferPreview()
            transferCommitted(count)
        }
    }

    function commitDraggedClip() {
        if (draggingClipId.length > 0)
            commitTransfer([draggingClipId])
        draggingClipId = ""
    }

    Component.onCompleted: refreshSources("")

    Connections {
        target: root.timelineDocument

        function onPancakeSequencesChanged() {
            root.refreshSources(root.sourcePath)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: theme.space10

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space2

                Label {
                    text: "Pancake Timelines"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeDialogTitle
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: "Drag a clip from the upper saved sequence into the current edit, or transfer an exact In/Out range."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    elide: Text.ElideRight
                }
            }

            ActionButton {
                objectName: "timelinePancakeCloseButton"
                text: "Done"
                compact: true
                onClicked: root.closeRequested()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: theme.radiusSurface
            color: theme.alpha(theme.ink800, 0.46)
            border.width: 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: theme.space10
                spacing: theme.space8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    StatusChip {
                        text: "SOURCE"
                        tone: "info"
                        compact: true
                        detailText: "Read-only until a transfer is committed."
                    }

                    ToneComboBox {
                        id: sourceSequenceCombo
                        objectName: "timelinePancakeSourceSequenceCombo"
                        Layout.fillWidth: true
                        model: root.sourceLabels()
                        enabled: count > 0
                        onActivated: function(index) { root.loadSourceAt(index) }
                    }

                    Label {
                        text: root.sourcePreview.available === true
                              ? Number(root.sourcePreview.frameRate || 0).toFixed(3).replace(/\.000$/, "") + " fps"
                              : "No other saved sequence"
                        color: theme.textFaint
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                Rectangle {
                    objectName: "timelinePancakeSourceTimeline"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(250, Math.max(116, sourceTrackList.contentHeight + theme.space4))
                    radius: theme.radiusControl
                    color: theme.alpha(theme.ink950, 0.56)
                    border.width: 0
                    clip: true

                    ListView {
                        id: sourceTrackList
                        anchors.fill: parent
                        model: root.sourcePreview.tracks || []
                        clip: true
                        spacing: 2
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                        delegate: Rectangle {
                            id: sourceTrackRow
                            required property var modelData
                            width: ListView.view.width
                            height: 52
                            color: theme.alpha(theme.ink800, 0.40)

                            Label {
                                width: 58
                                anchors.left: parent.left
                                anchors.leftMargin: theme.space8
                                anchors.verticalCenter: parent.verticalCenter
                                text: String(sourceTrackRow.modelData.displayLabel || "V")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeCaption
                                font.weight: Font.DemiBold
                            }

                            Item {
                                id: sourceLane
                                anchors.left: parent.left
                                anchors.leftMargin: 64
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 3

                                Repeater {
                                    model: sourceTrackRow.modelData.clips || []

                                    delegate: Rectangle {
                                        id: sourceClip
                                        required property var modelData
                                        readonly property bool selected: root.selectedClipIds.indexOf(
                                                                            String(modelData.clipId || "")) >= 0
                                        readonly property real homeX: Number(modelData.startSeconds || 0) /
                                                                      root.sourceDuration * sourceLane.width
                                        x: homeX
                                        y: 2
                                        width: Math.max(30, Number(modelData.trimmedDurationSeconds || 0.1) /
                                                              root.sourceDuration * sourceLane.width)
                                        height: sourceLane.height - 4
                                        radius: theme.radius4
                                        color: selected
                                               ? theme.alpha(theme.accentCool400, 0.30)
                                               : theme.alpha(theme.accentCool400, 0.14)
                                        border.width: selected ? 1 : 0
                                        border.color: selected ? theme.accentCool300 : "transparent"
                                        Drag.active: clipDrag.active
                                        Drag.keys: ["timeline-pancake-clip"]
                                        Drag.supportedActions: Qt.CopyAction | Qt.MoveAction
                                        Drag.hotSpot.x: width / 2
                                        Drag.hotSpot.y: height / 2

                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: theme.space6
                                            anchors.rightMargin: theme.space6
                                            text: String(sourceClip.modelData.displayLabel || "Clip")
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }

                                        TapHandler {
                                            onTapped: root.selectOnlyClip(String(sourceClip.modelData.clipId || ""))
                                        }

                                        DragHandler {
                                            id: clipDrag
                                            target: sourceClip
                                            onActiveChanged: {
                                                if (active) {
                                                    root.draggingClipId = String(sourceClip.modelData.clipId || "")
                                                    root.selectOnlyClip(root.draggingClipId)
                                                } else {
                                                    sourceClip.x = sourceClip.homeX
                                                    sourceClip.y = 2
                                                }
                                            }
                                        }

                                        AppToolTip {
                                            id: sharedTooltip1
                                            targetItem: sourceClip
                                            visible: sourceClip.visible && (clipHover.hovered)
                                            text: String(sourceClip.modelData.displayLabel || "Clip") +
                                                      "  ·  " + root.timeText(Number(sourceClip.modelData.startSeconds || 0)) +
                                                      "  ·  " + root.timeText(Number(sourceClip.modelData.trimmedDurationSeconds || 0))
                                        }
                                        HoverHandler { id: clipHover }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        anchors.centerIn: parent
                        visible: sourceTrackList.count === 0
                        text: root.sourceRecords.length === 0
                              ? "Save another sequence to use pancake editing."
                              : String(root.sourcePreview.reason || "This sequence has no tracks.")
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space6

                    Label {
                        text: "Range"
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        font.weight: Font.DemiBold
                    }

                    ToneTextField {
                        objectName: "timelinePancakeSourceInField"
                        Layout.preferredWidth: 92
                        text: Number(root.sourceInSeconds).toFixed(3)
                        placeholderText: "In seconds"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: {
                            root.sourceInSeconds = Math.max(0, Number(text || 0))
                            root.selectedClipIds = []
                            root.updateTransferPreview()
                        }
                    }

                    ToneTextField {
                        objectName: "timelinePancakeSourceOutField"
                        Layout.preferredWidth: 92
                        text: Number(root.sourceOutSeconds).toFixed(3)
                        placeholderText: "Out seconds"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: {
                            root.sourceOutSeconds = Math.max(0, Number(text || 0))
                            root.selectedClipIds = []
                            root.updateTransferPreview()
                        }
                    }

                    Item { Layout.fillWidth: true }

                    FilterChip {
                        text: "Copy"
                        iconName: "copy"
                        active: root.transferMode === "copy"
                        onClicked: {
                            root.transferMode = "copy"
                            root.updateTransferPreview()
                        }
                    }

                    FilterChip {
                        text: "Move"
                        iconName: "move"
                        active: root.transferMode === "move"
                        tone: "warning"
                        tooltipText: "Move updates the saved source sequence. One Undo restores both sequences."
                        onClicked: {
                            root.transferMode = "move"
                            root.updateTransferPreview()
                        }
                    }

                    FilterChip {
                        text: "Overwrite"
                        active: root.editMode === "overwrite"
                        onClicked: {
                            root.editMode = "overwrite"
                            root.updateTransferPreview()
                        }
                    }

                    FilterChip {
                        text: "Insert"
                        active: root.editMode === "insert"
                        onClicked: {
                            root.editMode = "insert"
                            root.updateTransferPreview()
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    height: 1
                    color: theme.alpha(theme.lineSoft, 0.30)
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    StatusChip {
                        text: "DESTINATION"
                        tone: "positive"
                        compact: true
                        detailText: "The current sequence receives the transfer at the exact destination time."
                    }

                    Label {
                        Layout.fillWidth: true
                        text: String(root.timelineDocument.timelineName || "Current sequence")
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeBody
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }

                    ToneTextField {
                        objectName: "timelinePancakeDestinationField"
                        Layout.preferredWidth: 104
                        text: Number(root.destinationSeconds).toFixed(3)
                        placeholderText: "At seconds"
                        inputMethodHints: Qt.ImhFormattedNumbersOnly
                        onEditingFinished: {
                            root.destinationSeconds = Math.max(0, Number(text || 0))
                            root.updateTransferPreview()
                        }
                    }
                }

                Rectangle {
                    objectName: "timelinePancakeDestinationTimeline"
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(220, Math.max(104, destinationTrackList.contentHeight + theme.space4))
                    radius: theme.radiusControl
                    color: destinationDrop.containsDrag
                           ? theme.alpha(theme.success, 0.10)
                           : theme.alpha(theme.ink950, 0.56)
                    border.width: destinationDrop.containsDrag ? 1 : 0
                    border.color: destinationDrop.containsDrag ? theme.success : "transparent"
                    clip: true

                    DropArea {
                        id: destinationDrop
                        objectName: "timelinePancakeDestinationDropArea"
                        anchors.fill: parent
                        keys: ["timeline-pancake-clip"]
                        onDropped: function(drop) {
                            root.destinationSeconds = Math.max(
                                        0, (drop.x - 64) / Math.max(1, width - 64) * root.destinationDuration)
                            root.updateTransferPreview()
                            root.commitDraggedClip()
                            drop.acceptProposedAction()
                        }
                    }

                    ListView {
                        id: destinationTrackList
                        anchors.fill: parent
                        model: root.timelineDocument.sequenceTracks || []
                        interactive: contentHeight > height
                        clip: true
                        spacing: 2

                        delegate: Rectangle {
                            id: destinationTrackRow
                            required property var modelData
                            width: ListView.view.width
                            height: 48
                            color: "transparent"

                            Label {
                                width: 58
                                anchors.left: parent.left
                                anchors.leftMargin: theme.space8
                                anchors.verticalCenter: parent.verticalCenter
                                text: String(destinationTrackRow.modelData.displayLabel || "V")
                                color: theme.textMuted
                                font.family: theme.fontMono
                                font.pixelSize: theme.typeCaption
                            }

                            Item {
                                id: destinationLane
                                anchors.left: parent.left
                                anchors.leftMargin: 64
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.bottom: parent.bottom
                                anchors.margins: 3

                                Repeater {
                                    model: destinationTrackRow.modelData.clips || []

                                    delegate: Rectangle {
                                        required property var modelData
                                        x: Number(modelData.startSeconds || 0) / root.destinationDuration * destinationLane.width
                                        y: 2
                                        width: Math.max(24, Number(modelData.trimmedDurationSeconds || 0.1) /
                                                              root.destinationDuration * destinationLane.width)
                                        height: destinationLane.height - 4
                                        radius: theme.radius4
                                        color: theme.alpha(theme.success, 0.13)
                                        border.width: 0

                                        Label {
                                            anchors.fill: parent
                                            anchors.leftMargin: theme.space6
                                            anchors.rightMargin: theme.space6
                                            text: String(parent.modelData.displayLabel || "Clip")
                                            color: theme.textMain
                                            font.family: theme.fontUi
                                            font.pixelSize: theme.typeCaption
                                            verticalAlignment: Text.AlignVCenter
                                            elide: Text.ElideRight
                                        }
                                    }
                                }

                                Rectangle {
                                    x: Number(root.destinationSeconds) / root.destinationDuration * parent.width
                                    width: 1
                                    height: parent.height
                                    color: theme.accentWarm300
                                }
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    Label {
                        Layout.fillWidth: true
                        text: root.transferPreview.available === true
                              ? String(root.transferPreview.summary || "Ready") +
                                (root.transferPreview.frameRateConversion === true
                                 ? " Source frames are retained; placement is snapped to the destination frame grid."
                                 : "")
                              : String(root.transferPreview.reason || "Select a clip or enter a range.")
                        color: root.transferPreview.available === true ? theme.textMuted : theme.textFaint
                        font.family: theme.fontUi
                        font.pixelSize: theme.typeCaption
                        wrapMode: Text.Wrap
                    }

                    ActionButton {
                        objectName: "timelinePancakeTransferButton"
                        text: root.transferMode === "move" ? "Move to Current" : "Copy to Current"
                        iconName: root.transferMode === "move" ? "move" : "copy"
                        tone: root.transferMode === "move" ? "warning" : "primary"
                        enabled: root.transferPreview.available === true
                        disabledReason: String(root.transferPreview.reason || "Select source material first.")
                        onClicked: root.commitTransfer(root.selectedClipIds)
                    }
                }
            }
        }
    }
}
