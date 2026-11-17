import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Effects
import QtQuick.Layouts

Rectangle {
    id: root

    required property var modelData
    property bool actionEnabled: false
    property string actionLabel: "Add"
    property string previewSource: ""
    property var exampleController: null
    property bool favorite: false
    property bool compatible: true
    property bool recent: false

    readonly property string effectName: String(modelData.name || "")
    readonly property string effectCategory: String(modelData.category || "")
    readonly property string effectDescription: String(modelData.description || "")
    readonly property string effectIcon: String(modelData.iconName || "effects")
    readonly property bool rendererAvailable: modelData.available !== false
    readonly property bool effectAvailable: rendererAvailable && compatible
    readonly property string sharedType: String(modelData.sharedPhotoType || "")
    readonly property string exampleSource: sharedType.length > 0 && exampleController
            && typeof exampleController.effectExampleThumbnailSource === "function"
            ? exampleController.effectExampleThumbnailSource(sharedType, false) : ""
    readonly property string timelineEffectLibraryRowContract: "timeline-effect-library-row:v5|row-height:30|thumbnail:32x22-cached-engine-examples|thumbnail-source:72x48-max|legacy-only-live-shader:true|drag:true|add:true|favorite:true|recent:true|compatible:true|animation:none|keyboard:true|hover:true|readable-text:true|tooltip-disclosed:true"
    readonly property string readableTextContract: "readable-text:v1|title-single-line:true|tooltip-full:true|accessibility-full:true"

    signal addRequested(string effectName)
    signal favoriteToggleRequested(string effectName)

    Theme { id: theme }

    readonly property real compactRowHeight: theme.scaled(30)
    objectName: "timelineEffectLibraryRow-" + effectName
    width: ListView.view ? ListView.view.width : 280
    height: compactRowHeight
    radius: theme.scaled(3)
    color: effectHover.hovered || activeFocus
           ? theme.alpha(theme.ink800, 0.94)
           : theme.alpha(theme.ink900, 0.48)
    border.width: activeFocus ? theme.focusBorderWidth : theme.hairlineWidth
    border.color: activeFocus ? theme.alpha(theme.focusRing, 0.9)
                               : effectHover.hovered ? theme.alpha(theme.lineSoft, 0.8)
                                                     : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: effectName
    Accessible.description: effectCategory + ". " + effectDescription
                            + (favorite ? " Favorite." : "")
                            + (recent ? " Recently used." : "")
                            + (effectAvailable ? " Compatible." : " Unavailable for this clip.")
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: function(event) {
        if (actionEnabled && effectAvailable) root.addRequested(effectName)
        event.accepted = true
    }
    Keys.onEnterPressed: function(event) {
        if (actionEnabled && effectAvailable) root.addRequested(effectName)
        event.accepted = true
    }
    Keys.onSpacePressed: function(event) {
        if (actionEnabled && effectAvailable) root.addRequested(effectName)
        event.accepted = true
    }

    property string libraryType: "effect"
    property string libraryName: effectName
    property real libraryDurationSeconds: 0.35
    Drag.active: effectDrag.active && effectAvailable
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["effect"]

    AppToolTip {
        id: effectTip
        targetItem: root
        visible: root.visible && effectHover.hovered
        delay: 350
        timeout: 3600
        text: root.effectName
                + (root.effectCategory.length > 0 ? " · " + root.effectCategory : "")
                + (root.effectDescription.length > 0 ? "\n" + root.effectDescription : "")
        implicitWidth: root.exampleSource.length > 0 ? theme.scaled(300)
                                                    : preferredTextWidth + leftPadding + rightPadding
        implicitHeight: exampleContent.implicitHeight + topPadding + bottomPadding
        contentItem: Column {
            id: exampleContent
            width: effectTip.availableWidth
            spacing: theme.space6
            Row {
                visible: root.exampleSource.length > 0
                width: parent.width
                spacing: theme.space4
                Repeater {
                    model: ["Original", root.effectName]
                    Column {
                        required property int index
                        required property string modelData
                        width: (exampleContent.width - theme.space4) / 2
                        spacing: theme.space2
                        Image {
                            width: parent.width
                            height: width * 0.675
                            source: effectTip.visible && root.exampleSource.length > 0
                                    ? (parent.index === 0
                                       ? root.exampleController.exampleThumbnailOriginalSource()
                                       : root.exampleSource) : ""
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectCrop
                            sourceSize.width: 320
                            sourceSize.height: 216
                        }
                        Text {
                            text: parent.modelData
                            width: parent.width
                            elide: Text.ElideRight
                            color: theme.textMuted
                            font.family: theme.fontUi
                            font.pixelSize: theme.typeMicro
                        }
                    }
                }
            }
            Text {
                text: effectTip.text
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
            }
            Text {
                visible: root.exampleSource.length > 0
                text: qsTr("Example image · adjust the result in Controls")
                width: parent.width
                wrapMode: Text.Wrap
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeMicro
            }
        }
    }

    HoverHandler { id: effectHover }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: theme.space3
        anchors.rightMargin: theme.space2
        spacing: theme.space3

        Rectangle {
            objectName: "timelineEffectLiveThumbnail"
            Layout.preferredWidth: theme.scaled(32)
            Layout.preferredHeight: theme.scaled(22)
            Layout.alignment: Qt.AlignVCenter
            radius: theme.scaled(2)
            color: theme.alpha(theme.ink950, 0.92)
            border.color: theme.alpha(theme.lineSoft, 0.45)
            border.width: theme.hairlineWidth
            clip: true

            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0; color: theme.alpha(theme.accentCool400, 0.34) }
                    GradientStop { position: 0.5; color: theme.alpha(theme.accentWarm400, 0.18) }
                    GradientStop { position: 1; color: theme.alpha(theme.ink700, 0.78) }
                }
            }

            Image {
                id: thumbnailImage
                anchors.fill: parent
                visible: root.sharedType.length > 0
                source: root.sharedType.length > 0 ? root.exampleSource : root.previewSource
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 72
                sourceSize.height: 48
            }

            Loader {
                objectName: "timelineEffectLegacyThumbnailShader"
                anchors.fill: parent
                active: root.sharedType.length === 0 && root.previewSource.length > 0
                        && thumbnailImage.status === Image.Ready
                sourceComponent: MultiEffect {
                source: thumbnailImage
                autoPaddingEnabled: false
                brightness: root.effectName === "Exposure" ? 0.24
                            : root.effectName === "Fade" ? 0.08
                            : root.effectName === "Punch" ? 0.04 : 0
                contrast: root.effectName === "Contrast" ? 0.32
                          : root.effectName === "Fade" ? -0.22
                          : root.effectName === "Punch" ? 0.28 : 0
                saturation: root.effectName === "Saturation" ? 0.48
                            : root.effectName === "Black & White" ? -1
                            : root.effectName === "Fade" ? -0.16
                            : root.effectName === "Punch" ? 0.34 : 0
                blurEnabled: root.effectName === "Blur"
                blur: root.effectName === "Blur" ? 0.58 : 0
                blurMax: 16
                }
            }

            AppIcon {
                anchors.centerIn: parent
                visible: thumbnailImage.source.toString().length === 0 || thumbnailImage.status !== Image.Ready
                name: root.effectIcon
                color: root.effectAvailable ? theme.accentCool300 : theme.textFaint
                implicitWidth: theme.space12
                implicitHeight: theme.space12
            }
        }

        Label {
            id: titleLabel
            Layout.fillWidth: true
            text: root.effectName
            color: root.effectAvailable ? theme.textStrong : theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            font.weight: Font.Medium
            elide: Text.ElideRight
            Accessible.name: root.effectName
        }

        AppIcon {
            visible: root.recent
            name: "clock"
            color: theme.textFaint
            implicitWidth: theme.space12
            implicitHeight: theme.space12
            Accessible.name: qsTr("Recently used")
        }

        ActionButton {
            objectName: "timelineEffectFavoriteButton"
            Layout.preferredWidth: theme.controlHeightDense
            Layout.preferredHeight: theme.controlHeightDense
            showText: false
            iconName: "star"
            compact: true
            Accessible.name: root.favorite
                             ? qsTr("Remove %1 from favorites").arg(root.effectName)
                             : qsTr("Add %1 to favorites").arg(root.effectName)
            active: root.favorite
            enabled: root.rendererAvailable
            tooltipText: root.favorite ? qsTr("Remove %1 from favorites.").arg(root.effectName)
                                       : qsTr("Add %1 to favorites.").arg(root.effectName)
            onClicked: root.favoriteToggleRequested(root.effectName)
        }

        ActionButton {
            objectName: "timelineEffectAddButton"
            Layout.preferredWidth: theme.controlHeightDense
            Layout.preferredHeight: theme.controlHeightDense
            showText: false
            iconName: root.actionLabel === "Added" ? "check" : "plus"
            compact: true
            Accessible.name: root.actionLabel === "Added"
                             ? qsTr("%1 is already applied").arg(root.effectName)
                             : qsTr("Add %1 effect").arg(root.effectName)
            enabled: root.actionEnabled && root.effectAvailable
            tooltipText: root.actionEnabled
                         ? (root.effectAvailable
                            ? qsTr("Add %1 to the selected clip.").arg(root.effectName)
                            : root.rendererAvailable
                              ? qsTr("%1 is not compatible with the selected media type.").arg(root.effectName)
                              : qsTr("%1 is unavailable in the current renderer.").arg(root.effectName))
                         : root.actionLabel === "Added"
                           ? qsTr("%1 is already in the effect stack.").arg(root.effectName)
                           : qsTr("Select a clip before adding effects.")
            onClicked: root.addRequested(root.effectName)
        }
    }

    DragHandler {
        id: effectDrag
        enabled: root.effectAvailable
    }
}
