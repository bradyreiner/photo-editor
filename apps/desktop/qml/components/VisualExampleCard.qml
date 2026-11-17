import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AbstractButton {
    id: root

    property string title: ""
    property string description: ""
    property string thumbnailSource: ""
    property string originalSource: ""
    property string badgeText: ""
    property string disabledReason: ""
    property bool selected: false
    property bool favorite: false
    property bool favoriteAvailable: false
    property bool showOriginal: false
    property bool previewActive: true
    property bool compact: false
    property string favoriteButtonObjectName: root.objectName + "Favorite"
    property string compareButtonObjectName: root.objectName + "Original"
    readonly property bool comparingOriginal: (root.showOriginal || compareButton.checked)
        && root.originalSource.length > 0
    readonly property string displayedSource: !root.previewActive ? ""
        : root.comparingOriginal ? root.originalSource : root.thumbnailSource
    readonly property bool previewReady: exampleImage.status === Image.Ready
    readonly property bool previewFailed: exampleImage.status === Image.Error
    readonly property int thumbnailStatus: exampleImage.status
    signal favoriteToggled()

    Theme { id: theme }
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: theme.space6
    implicitWidth: theme.scaled(180)
    implicitHeight: cardContents.implicitHeight + topPadding + bottomPadding
    Accessible.role: Accessible.Button
    Accessible.name: root.title
    Accessible.description: (!root.enabled && root.disabledReason.length > 0
        ? root.disabledReason + ". " : "") + root.description
        + (root.selected ? qsTr(" Selected.") : "")

    background: Rectangle {
        radius: theme.radiusPanel
        color: root.selected ? theme.editorSelectionFill
            : root.hovered ? theme.ink800 : theme.ink850
        border.width: root.activeFocus || root.selected ? 2 : theme.hairlineWidth
        border.color: root.activeFocus ? theme.focusRing
            : root.selected ? theme.lineStrong : theme.lineSoft
    }

    contentItem: ColumnLayout {
        id: cardContents
        spacing: theme.space6

        Rectangle {
            id: imageFrame
            objectName: root.objectName + "ImageFrame"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.preferredHeight: Math.max(theme.scaled(root.compact ? 66 : 82),
                root.availableWidth / (root.compact ? 1.9 : 1.6))
            color: theme.ink950
            radius: theme.radiusControl
            clip: true

            Image {
                id: exampleImage
                objectName: root.objectName + "Thumbnail"
                anchors.fill: parent
                source: root.displayedSource
                sourceSize.width: 640
                sourceSize.height: 432
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: true
                smooth: true
                Accessible.ignored: true
            }

            BusyIndicator {
                anchors.centerIn: parent
                width: theme.scaled(28)
                height: width
                running: root.previewActive && root.displayedSource.length > 0
                    && exampleImage.status === Image.Loading
                visible: running
            }

            Label {
                objectName: root.objectName + "PreviewNotice"
                anchors.centerIn: parent
                width: Math.max(0, parent.width - theme.space16)
                visible: root.previewFailed || root.displayedSource.length === 0
                text: root.previewFailed ? qsTr("Preview unavailable")
                    : qsTr("Select to try this look")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: theme.typeCaption
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: theme.space4
                width: theme.scaled(24)
                height: width
                radius: width / 2
                color: theme.ink850
                visible: root.selected
                AppIcon {
                    anchors.centerIn: parent
                    width: theme.scaled(15)
                    height: width
                    name: "check"
                    color: theme.textStrong
                }
            }

            Row {
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: theme.space4
                spacing: theme.space2
                MiniButton {
                    id: compareButton
                    objectName: root.compareButtonObjectName
                    visible: root.originalSource.length > 0 && !root.showOriginal
                    square: true
                    checkable: true
                    iconName: "compare"
                    role: "icon"
                    active: checked
                    Accessible.name: checked
                        ? qsTr("Show processed example for %1").arg(root.title)
                        : qsTr("Show original sample for %1").arg(root.title)
                    tooltip: Accessible.name
                }
                MiniButton {
                    id: favoriteButton
                    objectName: root.favoriteButtonObjectName
                    visible: root.favoriteAvailable
                    square: true
                    iconName: root.favorite ? "rating" : "star"
                    role: "icon"
                    active: root.favorite
                    Accessible.name: root.favorite
                        ? qsTr("Remove %1 from favorites").arg(root.title)
                        : qsTr("Favorite %1").arg(root.title)
                    tooltip: Accessible.name
                    onClicked: root.favoriteToggled()
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.margins: theme.space4
                visible: root.comparingOriginal || root.badgeText.length > 0
                width: Math.min(parent.width - theme.space8,
                    badgeLabel.implicitWidth + theme.space12)
                height: badgeLabel.implicitHeight + theme.space4
                radius: theme.radiusChip
                color: theme.alpha(theme.ink950, 0.86)
                Label {
                    id: badgeLabel
                    anchors.centerIn: parent
                    width: Math.max(0, parent.width - theme.space12)
                    text: root.comparingOriginal ? qsTr("Original") : root.badgeText
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeMicro
                    elide: Text.ElideRight
                }
            }
        }

        Label {
            objectName: root.objectName + "Title"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: root.title
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeLabel
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
            maximumLineCount: 2
            elide: Text.ElideRight
            Accessible.ignored: true
        }
        Label {
            objectName: root.objectName + "Description"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            visible: root.description.length > 0
            text: root.description
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeCaption
            wrapMode: Text.WordWrap
            maximumLineCount: root.compact ? 2 : 3
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }

    AppToolTip {
        targetItem: root
        visible: root.hovered && !compareButton.hovered && !favoriteButton.hovered
        text: !root.enabled && root.disabledReason.length > 0 ? root.disabledReason
            : root.title + (root.description.length > 0 ? "\n" + root.description : "")
    }
}
