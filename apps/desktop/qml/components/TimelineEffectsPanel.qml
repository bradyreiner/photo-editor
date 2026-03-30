import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
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
    color: theme.alpha(theme.ink900, 0.82)
    border.width: 1
    border.color: theme.alpha(theme.lineSoft, 0.55)

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
                                            elide: Text.ElideRight
                                        }

                                        ActionButton {
                                            text: "Remove"
                                            compact: true
                                            enabled: root.hasSelection
                                            onClicked: if (root.documentController) root.documentController.removeSelectedClipEffect(modelData)
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

                        delegate: Item {
                            required property var modelData
                            readonly property bool matchesSearch: root.effectSearchText.length === 0
                                                                 || String(modelData.name).toLowerCase().indexOf(root.effectSearchText.toLowerCase()) >= 0
                                                                 || String(modelData.category).toLowerCase().indexOf(root.effectSearchText.toLowerCase()) >= 0
                            visible: matchesSearch
                            width: ListView.view.width
                            height: visible ? 62 : 0

                            Rectangle {
                                id: effectTile
                                anchors.fill: parent
                                radius: 8
                                color: theme.alpha(theme.ink900, 0.88)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.34)
                                property string libraryType: "effect"
                                property string libraryName: modelData.name
                                property real libraryDurationSeconds: 0.35
                                Drag.active: effectDrag.active
                                Drag.hotSpot.x: width / 2
                                Drag.hotSpot.y: height / 2
                                Drag.dragType: Drag.Automatic
                                Drag.supportedActions: Qt.CopyAction
                                Drag.keys: ["effect"]

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name + "  •  " + modelData.category
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.description
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            wrapMode: Text.Wrap
                                        }
                                    }

                                    ActionButton {
                                        text: "Add"
                                        compact: true
                                        enabled: root.hasSelection
                                        onClicked: root.addQuickEffect(modelData.name)
                                    }
                                }

                                DragHandler {
                                    id: effectDrag
                                }
                            }
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

                        delegate: Item {
                            required property var modelData
                            readonly property bool matchesSearch: root.transitionSearchText.length === 0
                                                                 || String(modelData.name).toLowerCase().indexOf(root.transitionSearchText.toLowerCase()) >= 0
                            visible: matchesSearch
                            width: ListView.view.width
                            height: visible ? 60 : 0

                            Rectangle {
                                id: transitionTile
                                anchors.fill: parent
                                radius: 8
                                color: theme.alpha(theme.ink900, 0.88)
                                border.width: 1
                                border.color: theme.alpha(theme.lineSoft, 0.34)
                                property string libraryType: "transition"
                                property string libraryName: modelData.name
                                property real libraryDurationSeconds: modelData.name === "Cut" ? 0 : 0.35
                                Drag.active: transitionDrag.active
                                Drag.hotSpot.x: width / 2
                                Drag.hotSpot.y: height / 2
                                Drag.dragType: Drag.Automatic
                                Drag.supportedActions: Qt.CopyAction
                                Drag.keys: ["transition"]

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8

                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            color: theme.textStrong
                                            font.family: theme.fontUi
                                            font.pixelSize: 11
                                            font.weight: Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.description
                                            color: theme.textMuted
                                            font.family: theme.fontMono
                                            font.pixelSize: 9
                                            wrapMode: Text.Wrap
                                        }
                                    }

                                    ColumnLayout {
                                        spacing: 4

                                        ActionButton {
                                            text: "In"
                                            compact: true
                                            enabled: root.hasSelection
                                            onClicked: root.assignTransition("in", modelData.name, transitionTile.libraryDurationSeconds)
                                        }

                                        ActionButton {
                                            text: "Out"
                                            compact: true
                                            enabled: root.hasSelection
                                            onClicked: root.assignTransition("out", modelData.name, transitionTile.libraryDurationSeconds)
                                        }
                                    }
                                }

                                DragHandler {
                                    id: transitionDrag
                                }
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

                    Label {
                        text: "Scale  •  " + Number(root.hasClip ? (clipState.frameScale || 1) : 1).toFixed(2) + "x"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 1
                        to: 2.5
                        value: root.hasClip && clipState.frameScale ? clipState.frameScale : 1
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedFrameScale(value)
                    }

                    Label {
                        text: "Horizontal  •  " + Number(root.hasClip ? (clipState.frameOffsetX || 0) : 0).toFixed(2)
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: -1
                        to: 1
                        value: root.hasClip && clipState.frameOffsetX ? clipState.frameOffsetX : 0
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedFrameOffsetX(value)
                    }

                    Label {
                        text: "Vertical  •  " + Number(root.hasClip ? (clipState.frameOffsetY || 0) : 0).toFixed(2)
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: -1
                        to: 1
                        value: root.hasClip && clipState.frameOffsetY ? clipState.frameOffsetY : 0
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedFrameOffsetY(value)
                    }

                    Label {
                        text: "Opacity  •  " + Math.round(Number(root.hasClip ? (clipState.opacity || 1) : 1) * 100) + "%"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: 1
                        value: root.hasClip && clipState.opacity !== undefined ? clipState.opacity : 1
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedClipOpacity(value)
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

                    Label {
                        text: "Gain  •  " + Number(root.hasClip ? (clipState.audioGainDb || 0) : 0).toFixed(1) + " dB"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: -36
                        to: 12
                        value: root.hasClip && clipState.audioGainDb !== undefined ? clipState.audioGainDb : 0
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedClipAudioGain(value)
                    }

                    Label {
                        text: "Fade In  •  " + Number(root.hasClip ? (clipState.fadeInSeconds || 0) : 0).toFixed(2) + "s"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                        value: root.hasClip && clipState.fadeInSeconds !== undefined ? clipState.fadeInSeconds : 0
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedClipFadeInSeconds(value)
                    }

                    Label {
                        text: "Fade Out  •  " + Number(root.hasClip ? (clipState.fadeOutSeconds || 0) : 0).toFixed(2) + "s"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: 10
                    }

                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: root.hasClip && clipState.trimmedDurationSeconds ? clipState.trimmedDurationSeconds : 1
                        value: root.hasClip && clipState.fadeOutSeconds !== undefined ? clipState.fadeOutSeconds : 0
                        enabled: root.hasSelection
                        onMoved: if (root.documentController) root.documentController.setSelectedClipFadeOutSeconds(value)
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
