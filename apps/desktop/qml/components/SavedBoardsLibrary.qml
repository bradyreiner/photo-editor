pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "savedBoardsLibrary"
    spacing: 16
    property var documents: []
    signal openRequested(string documentId)
    signal createRequested()
    Theme { id: theme }

    function refresh() { documents = carouselDocument.recentDocuments() }
    property bool ready: false
    Component.onCompleted: { ready = true; if (visible) refresh() }
    onVisibleChanged: { if (ready && visible) refresh() }

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: "Saved Boards"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: root.documents.length + " document" + (root.documents.length === 1 ? "" : "s") + " · opens in Edit"; color: theme.textMuted; font.pixelSize: 12 }
        }
        ActionButton { compact: true; text: "Refresh"; iconName: "refresh"; onClicked: root.refresh() }
        ActionButton { compact: true; text: "New Boards"; iconName: "add"; onClicked: root.createRequested() }
    }

    GridView {
        id: grid
        objectName: "savedBoardsGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.documents
        cellWidth: width / Math.max(1, Math.floor(width / 290))
        cellHeight: 238
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            required property var modelData
            width: grid.cellWidth - 14
            height: grid.cellHeight - 14
            radius: 4
            color: theme.surfaceInset
            border.color: theme.lineSoft
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 82
                    spacing: 5
                    Repeater {
                        model: Math.min(5, Math.max(1, Number(modelData.slideCount || 1)))
                        Rectangle { Layout.fillWidth: true; Layout.maximumWidth: 42; Layout.preferredHeight: 70; radius: 2; color: theme.alpha(theme.textStrong, 0.08); border.color: theme.alpha(theme.textStrong, 0.16) }
                    }
                }
                Label { Layout.fillWidth: true; text: String(modelData.name || "Untitled Boards"); color: theme.textStrong; font.pixelSize: 15; font.weight: Font.DemiBold; wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space6
                    MediaTypeIcon {
                        mediaType: "carousel"
                        color: theme.textMuted
                        Layout.preferredWidth: theme.scaled(16)
                        Layout.preferredHeight: theme.scaled(16)
                    }
                    Label {
                        Layout.fillWidth: true
                        text: Number(modelData.slideCount || 0) + " boards · " + String(modelData.documentFormat || "custom").toUpperCase()
                        color: theme.textMuted
                        font.pixelSize: 11
                    }
                }
                Item { Layout.fillHeight: true }
                ActionButton {
                    objectName: "libraryOpenBoardsButton" + String(modelData.documentId || "")
                    Layout.fillWidth: true
                    compact: true
                    text: "Open in Edit"
                    iconName: "chevron-right"
                    onClicked: root.openRequested(String(modelData.documentId || ""))
                }
            }
        }
        Label {
            anchors.centerIn: parent
            visible: root.documents.length === 0
            text: "Start in Edit, then choose Add boards.\nYour saved board documents will appear here."
            color: theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.5
        }
    }
}
