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
    property bool favorite: false
    property bool compatible: true
    property bool recent: false
    readonly property string effectName: String(modelData.name || "")
    readonly property string effectCategory: String(modelData.category || "")
    readonly property string effectDescription: String(modelData.description || "")
    readonly property string effectIcon: String(modelData.iconName || "effects")
    readonly property bool rendererAvailable: modelData.available !== false
    readonly property bool effectAvailable: rendererAvailable && compatible
    readonly property string timelineEffectLibraryRowContract: "timeline-effect-library-row:v2|drag:true|add:true|favorite:true|recent:true|compatible:true|thumbnail:bounded-live-current-source|thumbnail-size:144x88-max|animation:none|keyboard:true|hover:true|readable-text:true|no-elide:true"
    readonly property string readableTextContract: "readable-text:v1|ellipsis:none|title-wrap:true|description-wrap:true|tooltip-full:true|accessibility-full:true"

    signal addRequested(string effectName)
    signal favoriteToggleRequested(string effectName)

    Theme { id: theme }

    width: ListView.view ? ListView.view.width : 280
    height: 88
    radius: theme.radius8
    color: effectHover.hovered || activeFocus
           ? theme.alpha(theme.ink850, 0.96)
           : theme.alpha(theme.ink900, 0.88)
    border.width: activeFocus ? theme.focusBorderWidth : 0
    border.color: activeFocus ? theme.alpha(theme.focusRing, 0.9) : "transparent"
    clip: true
    activeFocusOnTab: true
    Accessible.name: effectName
    Accessible.description: effectCategory + ". " + effectDescription
                            + (favorite ? " Favorite." : "")
                            + (effectAvailable ? " Compatible." : " Unavailable for this clip.")
    Accessible.role: Accessible.ListItem
    Keys.onReturnPressed: if (actionEnabled) root.addRequested(root.effectName)
    Keys.onEnterPressed: if (actionEnabled) root.addRequested(root.effectName)
    Keys.onSpacePressed: if (actionEnabled) root.addRequested(root.effectName)

    property string libraryType: "effect"
    property string libraryName: effectName
    property real libraryDurationSeconds: 0.35
    Drag.active: effectDrag.active && effectAvailable
    Drag.hotSpot.x: width / 2
    Drag.hotSpot.y: height / 2
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: ["effect"]

    ToolTip.visible: effectHover.hovered && titleLabel.contentHeight > titleLabel.height + 1
    ToolTip.delay: 350
    ToolTip.timeout: 3600
    ToolTip.text: effectName + (effectCategory.length > 0 ? " - " + effectCategory : "")

    HoverHandler { id: effectHover }

    RowLayout {
        anchors.fill: parent
        anchors.margins: theme.space8
        spacing: theme.space8

        Rectangle {
            id: effectThumbnail
            objectName: "timelineEffectLiveThumbnail"
            Layout.preferredWidth: theme.scaled(52)
            Layout.preferredHeight: theme.scaled(44)
            radius: theme.radius4
            color: theme.alpha(theme.ink950, 0.92)
            border.width: 0
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
                visible: false
                source: root.previewSource
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectCrop
                sourceSize.width: 144
                sourceSize.height: 88
            }

            MultiEffect {
                anchors.fill: parent
                source: thumbnailImage
                visible: root.previewSource.length > 0 && thumbnailImage.status === Image.Ready
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

            AppIcon {
                anchors.centerIn: parent
                visible: root.previewSource.length === 0 || thumbnailImage.status !== Image.Ready
                name: root.effectIcon
                color: root.effectAvailable ? theme.accentCool300 : theme.textFaint
                implicitWidth: theme.space14
                implicitHeight: theme.space14
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: theme.space2

            RowLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Label {
                    id: titleLabel
                    Layout.fillWidth: true
                    text: root.effectName
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeBody
                    font.weight: Font.Medium
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }

                Label {
                    text: root.effectCategory
                    visible: text.length > 0 && root.width >= 400
                    color: theme.textFaint
                    font.family: theme.fontMono
                    font.pixelSize: theme.typeCaption
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: 7
                    elide: Text.ElideNone
                }
            }

            Label {
                Layout.fillWidth: true
                text: root.effectDescription
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }

        ColumnLayout {
            Layout.preferredWidth: theme.scaled(64)
            Layout.maximumWidth: theme.scaled(64)
            spacing: 4
            ActionButton {
                objectName: "timelineEffectFavoriteButton"
                Layout.alignment: Qt.AlignRight
                Layout.preferredWidth: theme.controlHeightDense
                showText: false
                iconName: "star"
                compact: true
                active: root.favorite
                enabled: root.rendererAvailable
                tooltipText: root.favorite ? "Remove " + root.effectName + " from favorites."
                                           : "Add " + root.effectName + " to favorites."
                onClicked: root.favoriteToggleRequested(root.effectName)
            }
    
            ActionButton {
                text: root.actionLabel
                compact: true
                enabled: root.actionEnabled && root.effectAvailable
                tooltipText: root.actionEnabled
                             ? (root.effectAvailable
                                ? "Add this effect to the selected clip."
                                : root.rendererAvailable
                                  ? "This effect is not compatible with the selected media type."
                                  : "This effect is unavailable in the current renderer.")
                             : root.actionLabel === "Added"
                               ? "This effect is already in the selected clip's stack."
                               : "Select a clip before adding effects."
                onClicked: root.addRequested(root.effectName)
            }
        }

    }

    DragHandler {
        id: effectDrag
        enabled: root.effectAvailable
    }
}
