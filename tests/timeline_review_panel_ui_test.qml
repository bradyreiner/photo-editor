import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../apps/desktop/qml/components"

ApplicationWindow {
    id: fixture

    objectName: "timelineReviewPanelFixture"
    width: 360
    height: 780
    visible: true
    color: theme.ink950
    property real lastJumpSeconds: -1

    Theme { id: theme }

    TimelineReviewPanel {
        id: reviewPanel
        objectName: "testedTimelineReviewPanel"
        anchors.fill: parent
        theme: theme
        documentController: testReviewDocument
        currentClip: ({"clipId": "clip-17", "displayLabel": "Portrait hero"})
        playheadSeconds: 3
        frameRate: 30
        inPointSeconds: 1
        outPointSeconds: 2
        onJumpRequested: function(seconds) {
            fixture.lastJumpSeconds = seconds
            testReviewDocument.seekSequenceSeconds(seconds)
        }
    }
}
