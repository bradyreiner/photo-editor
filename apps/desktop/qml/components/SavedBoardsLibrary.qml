pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "savedBoardsLibrary"
    spacing: 16
    readonly property var documents: carouselDocument.recentBoardDocuments.filter(document =>
        root.projectName === "all" || (root.projectName === "unassigned"
            ? !String(document.projectName || "").length : document.projectName === root.projectName))
    property string projectName: "all"
    signal openRequested(string documentId)
    signal createRequested()
    Theme { id: theme }

    function refresh(force) { carouselDocument.refreshRecentBoardDocuments(force === true) }
    Connections {
        target: carouselDocument
        function onDocumentStateChanged() { if (root.visible) root.refresh() }
    }
    property bool ready: false
    Component.onCompleted: { ready = true; if (visible) refresh() }
    onVisibleChanged: { if (ready && visible) refresh() }

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: "Saved Boards"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: carouselDocument.recentBoardDocumentsLoading ? "Loading saved boards…" : root.documents.length + " document" + (root.documents.length === 1 ? "" : "s") + " · opens in Edit"; color: theme.textMuted; font.pixelSize: 12 }
        }
        ActionButton { objectName: "libraryRefreshBoardsButton"; compact: true; text: "Refresh"; iconName: "refresh"; enabled: !carouselDocument.recentBoardDocumentsLoading; onClicked: root.refresh(true) }
        ActionButton { objectName: "libraryNewBoardsButton"; compact: true; text: "New Boards"; iconName: "add"; onClicked: root.createRequested() }
    }

    Label {
        objectName: "savedBoardsLoadError"
        Layout.fillWidth: true
        visible: carouselDocument.recentBoardDocumentsError.length > 0
        text: carouselDocument.recentBoardDocumentsError
        color: theme.warning
        wrapMode: Text.Wrap
    }

    GridView {
        id: grid
        objectName: "savedBoardsGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.documents
        cellWidth: width / Math.max(1, Math.floor(width / 290))
        cellHeight: theme.scaled(300)
        implicitHeight: cellHeight
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: Rectangle {
            required property var modelData
            function requestCover() {
                if (root.visible && modelData && modelData.documentId)
                    carouselDocument.requestBoardThumbnail(String(modelData.documentId))
            }
            Component.onCompleted: requestCover()
            onModelDataChanged: requestCover()
            width: grid.cellWidth - 14
            height: grid.cellHeight - 14
            radius: 4
            color: theme.surfaceInset
            border.color: theme.lineSoft
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: theme.scaled(112)
                    color: theme.mediaCanvas
                    Canvas {
                        anchors.fill: parent
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            const ctx = getContext("2d")
                            const size = 12
                            ctx.fillStyle = "#333333"; ctx.fillRect(0, 0, width, height)
                            ctx.fillStyle = "#3C3C3C"
                            for (let y = 0; y < height; y += size)
                                for (let x = 0; x < width; x += size)
                                    if ((x / size + y / size) % 2 === 0) ctx.fillRect(x, y, size, size)
                        }
                    }
                    Image {
                        id: boardCover
                        objectName: "savedBoardThumbnail"
                        anchors.fill: parent
                        source: String(modelData.previewSource || "")
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: theme.scaled(64)
                        height: theme.scaled(86)
                        color: "#F4F1E9"
                        visible: boardCover.source.toString().length === 0
                    }
                    OpticalActivityIndicator {
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.margins: theme.space8
                        visible: boardCover.status === Image.Loading
                        active: visible
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
            visible: root.documents.length === 0 && !carouselDocument.recentBoardDocumentsLoading && !carouselDocument.recentBoardDocumentsError.length
            text: "Choose New Boards to start a canvas in any size.\nYour saved board documents will appear here."
            color: theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.5
        }
    }
}
