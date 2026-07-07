import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

GlassSurface {
    id: root

    required property var theme
    required property var timelineDocument
    required property var currentClip
    required property bool collapsed
    required property string activeTab
    required property var effectLibraryModel
    required property var transitionLibraryModel
    signal toggleCollapseRequested()
    signal tabChangeRequested(string tab)

    property string effectSearchText: ""
    property string transitionSearchText: ""
    readonly property var documentController: timelineDocument !== undefined && timelineDocument !== null ? timelineDocument : null
    readonly property var clipState: currentClip !== undefined && currentClip !== null ? currentClip : ({})
    readonly property var effectLibraryItems: effectLibraryModel !== undefined && effectLibraryModel !== null ? effectLibraryModel : []
    readonly property var transitionLibraryItems: transitionLibraryModel !== undefined && transitionLibraryModel !== null ? transitionLibraryModel : []
    readonly property int selectedClipIndex: documentController !== null ? Number(documentController.currentIndex) : -1
    readonly property bool hasSelection: selectedClipIndex >= 0

    radius: 12
    accent: theme.accentWarm400
    accentRail: false
    elevated: true

    readonly property bool hasClip: clipState.displayLabel !== undefined

    function currentEffectStack() {
        return hasClip && clipState.effectStack ? clipState.effectStack : []
    }

    function addQuickEffect(effectName) {
        if (!hasSelection || documentController === null)
            return
        documentController.addSelectedClipEffect(effectName)
    }

    function assignTransition(edge, transitionName, durationSeconds) {
        if (!hasSelection || documentController === null)
            return
        documentController.assignSelectedClipTransition(edge, transitionName, durationSeconds)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            Label {
                text: "Effects"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                iconName: collapsed ? "chevron-left" : "chevron-right"
                showText: false
                compact: true
                tooltipText: collapsed ? "Expand inspector" : "Collapse inspector"
                onClicked: toggleCollapseRequested()
            }
        }

        ColumnLayout {
            visible: collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            ActionButton {
                Layout.fillWidth: true
                iconName: "effects"
                showText: false
                compact: true
                tooltipText: "Effects"
                active: activeTab === "effects"
                onClicked: tabChangeRequested("effects")
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "transition"
                showText: false
                compact: true
                tooltipText: "Transitions"
                active: activeTab === "transitions"
                onClicked: tabChangeRequested("transitions")
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "motion"
                showText: false
                compact: true
                tooltipText: "Motion"
                active: activeTab === "motion"
                onClicked: tabChangeRequested("motion")
            }

            ActionButton {
                Layout.fillWidth: true
                iconName: "audio"
                showText: false
                compact: true
                tooltipText: "Audio"
                active: activeTab === "audio"
                onClicked: tabChangeRequested("audio")
            }

            Item { Layout.fillHeight: true }
        }

        ColumnLayout {
            visible: !collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: root.hasClip && clipState.displayLabel ? clipState.displayLabel : "No clip selected"
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: 12
                wrapMode: Text.Wrap
            }

            Label {
                Layout.fillWidth: true
                text: root.hasClip && clipState.trackLabel ? clipState.trackLabel : "Select a clip to edit"
                color: theme.textMuted
                font.family: theme.fontMono
                font.pixelSize: 10
                wrapMode: Text.Wrap
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                ActionButton {
                    text: "Fx"
                    compact: true
                    active: activeTab === "effects"
                    onClicked: tabChangeRequested("effects")
                }

                ActionButton {
                    text: "Trans"
                    compact: true
                    active: activeTab === "transitions"
                    onClicked: tabChangeRequested("transitions")
                }

                ActionButton {
                    text: "Motion"
                    compact: true
                    active: activeTab === "motion"
                    onClicked: tabChangeRequested("motion")
                }

                ActionButton {
                    text: "Audio"
                    compact: true
                    active: activeTab === "audio"
                    onClicked: tabChangeRequested("audio")
                }

                ActionButton {
                    text: "Clip"
                    compact: true
                    active: activeTab === "clip"
                    onClicked: tabChangeRequested("clip")
                }
            }

            Rectangle {
                visible: activeTab === "effects"
                Layout.fillWidth: true
                Layout.fillHeight: activeTab === "effects"
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: "Active Stack"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        radius: 8
                        color: theme.alpha(theme.ink900, 0.84)
                        border.width: 1
                        border.color: theme.alpha(theme.lineSoft, 0.34)
                        Layout.preferredHeight: Math.min(200, Math.max(60, stackColumn.implicitHeight + 20))
                        clip: true

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            contentHeight: stackColumn.implicitHeight
                            boundsBehavior: Flickable.StopAtBounds
                            clip: true

                            ColumnLayout {
                                id: stackColumn
                                width: parent.width
                                spacing: 6

                                Repeater {
                                    model: root.currentEffectStack()

                                    delegate: RowLayout {
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 6

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData
                                            color: theme.textMain
                                            font.family: theme.fontMono
                                            font.pixelSize: 10
                                            fontSizeMode: Text.HorizontalFit
                                            minimumPixelSize: 7
                                            elide: Text.ElideNone
                                        }

                                        ConfirmActionButton {
                                            text: "Remove"
                                            compact: true
                                            enabled: root.hasSelection
                                            helperText: "Remove this effect from the selected clip"
                                            onConfirmed: if (root.documentController) root.documentController.removeSelectedClipEffect(modelData)
                                        }
                                    }
                                }

                                Label {
                                    visible: root.currentEffectStack().length === 0
                                    text: "No effects on this clip yet."
                                    color: theme.textMuted
                                    font.family: theme.fontMono
                                    font.pixelSize: 10
                                }
                            }
                        }
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6

                        Repeater {
                            model: ["Transform", "Opacity", "Crop", "Color Balance", "Gaussian Blur", "Levels", "Volume"]

                            delegate: ActionButton {
                                required property string modelData
                                text: modelData
                                compact: true
                                enabled: root.hasSelection
                                onClicked: root.addQuickEffect(modelData)
                            }
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: root.effectSearchText
                        placeholderText: "Search effects"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        selectByMouse: true
                        onTextChanged: root.effectSearchText = text

                        background: Rectangle {
                            radius: 8
                            color: theme.ink900
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.42)
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.effectLibraryItems

                        delegate: TimelineEffectLibraryRow {
                            searchText: root.effectSearchText
                            actionEnabled: root.hasSelection
                            onAddRequested: function(effectName) { root.addQuickEffect(effectName) }
                        }
                    }
                }
            }

            Rectangle {
                visible: activeTab === "transitions"
                Layout.fillWidth: true
                Layout.fillHeight: activeTab === "transitions"
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "In  •  " + (root.hasClip && clipState.transitionInName ? clipState.transitionInName : "none")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        Item { Layout.fillWidth: true }

                        ActionButton {
                            text: "Clear In"
                            compact: true
                            enabled: root.hasSelection
                            onClicked: if (root.documentController) root.documentController.clearSelectedClipTransition("in")
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Label {
                            text: "Out  •  " + (root.hasClip && clipState.transitionOutName ? clipState.transitionOutName : "none")
                            color: theme.textMuted
                            font.family: theme.fontMono
                            font.pixelSize: 10
                        }

                        Item { Layout.fillWidth: true }

                        ActionButton {
                            text: "Clear Out"
                            compact: true
                            enabled: root.hasSelection
                            onClicked: if (root.documentController) root.documentController.clearSelectedClipTransition("out")
                        }
                    }

                    TextField {
                        Layout.fillWidth: true
                        text: root.transitionSearchText
                        placeholderText: "Search transitions"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        selectByMouse: true
                        onTextChanged: root.transitionSearchText = text

                        background: Rectangle {
                            radius: 8
                            color: theme.ink900
                            border.width: 1
                            border.color: theme.alpha(theme.lineSoft, 0.42)
                        }
                    }

                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 6
                        model: root.transitionLibraryItems

                        delegate: TimelineTransitionLibraryRow {
                            searchText: root.transitionSearchText
                            actionEnabled: root.hasSelection
                            onTransitionRequested: function(edge, transitionName, durationSeconds) {
                                root.assignTransition(edge, transitionName, durationSeconds)
                            }
                        }
                    }
                }
            }

            Rectangle {
                visible: activeTab === "motion"
                Layout.fillWidth: true
                Layout.fillHeight: activeTab === "motion"
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: "Frame"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    ToneComboBox {
                        Layout.fillWidth: true
                        model: ["Fit", "Fill", "Stretch"]
                        currentIndex: root.hasClip && clipState.frameMode === "fill" ? 1 : root.hasClip && clipState.frameMode === "stretch" ? 2 : 0
                        enabled: root.hasSelection
                        onActivated: if (root.documentController) root.documentController.setSelectedFrameMode(currentText.toLowerCase())
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Scale"
                        valueText: Number(root.hasClip ? (clipState.frameScale || 1) : 1).toFixed(2) + "x"
                        from: 1
                        to: 2.5
                        value: root.hasClip && clipState.frameScale ? clipState.frameScale : 1
                        defaultValue: 1
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedFrameScale(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedFrameScale(1)
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Horizontal"
                        valueText: Number(root.hasClip ? (clipState.frameOffsetX || 0) : 0).toFixed(2)
                        from: -1
                        to: 1
                        value: root.hasClip && clipState.frameOffsetX ? clipState.frameOffsetX : 0
                        defaultValue: 0
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedFrameOffsetX(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedFrameOffsetX(0)
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Vertical"
                        valueText: Number(root.hasClip ? (clipState.frameOffsetY || 0) : 0).toFixed(2)
                        from: -1
                        to: 1
                        value: root.hasClip && clipState.frameOffsetY ? clipState.frameOffsetY : 0
                        defaultValue: 0
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedFrameOffsetY(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedFrameOffsetY(0)
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Opacity"
                        valueText: Math.round(Number(root.hasClip ? (clipState.opacity || 1) : 1) * 100) + "%"
                        from: 0
                        to: 1
                        value: root.hasClip && clipState.opacity !== undefined ? clipState.opacity : 1
                        defaultValue: 1
                        valueInputMode: "percentNormalized"
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedClipOpacity(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedClipOpacity(1)
                    }
                }
            }

            Rectangle {
                visible: activeTab === "audio"
                Layout.fillWidth: true
                Layout.fillHeight: activeTab === "audio"
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: "Audio"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Gain"
                        valueText: Number(root.hasClip ? (clipState.audioGainDb || 0) : 0).toFixed(1) + " dB"
                        from: -36
                        to: 12
                        value: root.hasClip && clipState.audioGainDb !== undefined ? clipState.audioGainDb : 0
                        defaultValue: 0
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedClipAudioGain(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedClipAudioGain(0)
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Fade In"
                        valueText: Number(root.hasClip ? (clipState.fadeInSeconds || 0) : 0).toFixed(2) + "s"
                        from: 0
                        to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                        value: root.hasClip && clipState.fadeInSeconds !== undefined ? clipState.fadeInSeconds : 0
                        defaultValue: 0
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedClipFadeInSeconds(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedClipFadeInSeconds(0)
                    }

                    InspectorSlider {
                        Layout.fillWidth: true
                        label: "Fade Out"
                        valueText: Number(root.hasClip ? (clipState.fadeOutSeconds || 0) : 0).toFixed(2) + "s"
                        from: 0
                        to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                        value: root.hasClip && clipState.fadeOutSeconds !== undefined ? clipState.fadeOutSeconds : 0
                        defaultValue: 0
                        enabled: root.hasSelection
                        onValueDragged: function(value) {
                            if (root.documentController)
                                root.documentController.setSelectedClipFadeOutSeconds(value)
                        }
                        onResetRequested: if (root.documentController) root.documentController.setSelectedClipFadeOutSeconds(0)
                    }
                }
            }

            Rectangle {
                visible: activeTab === "clip"
                Layout.fillWidth: true
                Layout.fillHeight: activeTab === "clip"
                radius: 10
                color: theme.alpha(theme.ink950, 0.72)
                border.width: 1
                border.color: theme.alpha(theme.lineSoft, 0.42)

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 6

                    Label {
                        text: "Clip Info"
                        color: theme.textStrong
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: "Trim In  •  " + Number(root.hasClip ? (clipState.trimStartSeconds || 0) : 0).toFixed(2) + "s"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: "Trim Out  •  " + Number(root.hasClip ? (clipState.trimEndSeconds || 0) : 0).toFixed(2) + "s"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: "Duration  •  " + (root.hasClip && clipState.trimmedDurationText ? clipState.trimmedDurationText : "0:00")
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Label {
                        text: "Playback Rate  •  " + Number(root.hasClip ? (clipState.playbackRate || 1) : 1).toFixed(2) + "x"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }
                }
            }
        }
    }
}
