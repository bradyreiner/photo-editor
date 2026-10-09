pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "savedTimelinesLibrary"
    spacing: 16
    readonly property var documents: timelineDocument.recentTimelineRecords
    readonly property bool loading: timelineDocument.recentTimelinesLoading
    property bool ready: false
    signal openRequested(string documentPath, string recoveryCandidateId)
    signal createRequested()
    Theme { id: theme }

    function refresh() { timelineDocument.refreshRecentTimelines() }
    function durationLabel(seconds) {
        const duration = Math.max(0, Math.round(Number(seconds || 0)))
        const minutes = Math.floor(duration / 60)
        const secondsLabel = String(duration % 60).padStart(2, "0")
        return minutes >= 60
             ? Math.floor(minutes / 60) + ":" + String(minutes % 60).padStart(2, "0") + ":" + secondsLabel
             : minutes + ":" + secondsLabel
    }
    function updatedLabel(value) {
        if (!value)
            return ""
        const date = new Date(String(value))
        return isNaN(date.getTime()) ? String(value) : Qt.formatDateTime(date, "MMM d, yyyy · h:mm AP")
    }
    Component.onCompleted: { ready = true; if (visible) refresh() }
    onVisibleChanged: { if (ready && visible) refresh() }

    RowLayout {
        Layout.fillWidth: true
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            Label { text: "Saved Timelines"; color: theme.textStrong; font.family: theme.fontUi; font.pixelSize: 20; font.weight: Font.DemiBold }
            Label { text: root.documents.length + " document" + (root.documents.length === 1 ? "" : "s") + " · opens in Timeline"; color: theme.textMuted; font.pixelSize: 12 }
        }
        ActionButton { objectName: "libraryRefreshTimelinesButton"; compact: true; text: "Refresh"; iconName: "refresh"; enabled: !root.loading; onClicked: root.refresh() }
        ActionButton { objectName: "libraryNewTimelineButton"; compact: true; text: "New Timeline"; iconName: "add"; onClicked: root.createRequested() }
    }

    InlineActivityIndicator {
        Layout.fillWidth: true
        visible: root.loading
        title: "Loading timelines"
    }

    ListView {
        id: list
        objectName: "savedTimelinesList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: root.documents
        spacing: theme.space8
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
        delegate: RecentDocumentRow {
            required property var modelData
            required property int index
            objectName: "libraryOpenTimelineRow" + index
            width: list.width
            height: Math.max(72, implicitHeight)
            title: String(modelData.name || "Untitled Timeline")
            iconName: "timeline"
            detail: (modelData.isRecovery ? "Recoverable work" : "Saved")
                    + " · " + root.durationLabel(modelData.durationSeconds)
                    + ((modelData.updatedAt || modelData.createdAt) ? " · " + root.updatedLabel(modelData.updatedAt || modelData.createdAt) : "")
            actionable: Boolean(modelData.recoveryCandidateId || modelData.documentPath)
            onActivated: root.openRequested(String(modelData.documentPath || ""), String(modelData.recoveryCandidateId || ""))
        }
        Label {
            anchors.centerIn: parent
            visible: root.documents.length === 0 && !root.loading
            text: "No saved timelines yet.\nCreate a timeline to start editing video."
            color: theme.textMuted
            horizontalAlignment: Text.AlignHCenter
            lineHeight: 1.5
        }
    }
}
