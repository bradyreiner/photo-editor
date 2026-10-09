import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "uiPrimitivesFeedbackPage"

    property int selectedNavigationPage: 0
    property bool sampleBusy: true
    property bool sampleFailed: false
    property real sampleProgress: 0.62
    property int retryCount: 0
    property bool sampleFolderSelected: false
    property string lastOpenedLocation: ""

    Theme { id: theme }

    spacing: theme.gapPanel
    Layout.fillWidth: true

    SectionHeader {
        Layout.fillWidth: true
        title: "Feedback and surfaces"
        subtitle: "Navigation, status, progress, and compact information components. Examples are local to this gallery."
        titlePixelSize: theme.typeSection
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.gapPanel

        StudioPanel {
            id: navigationPanel
            objectName: "uiPrimitivesFeedbackNavigationPanel"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: navigationContent.implicitHeight + theme.typeSection + (theme.space12 * 4)
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            title: "Navigation"
            subhead: "Destinations · sources · folders"

            ColumnLayout {
                id: navigationContent
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space4

                PreferenceNavRow {
                    objectName: "uiPrimitivesPreferenceNavLibrary"
                    Layout.fillWidth: true
                    pageIndex: 0
                    title: "Library"
                    subtitle: "Browse and organize media"
                    showSubtitle: false
                    selected: root.selectedNavigationPage === pageIndex
                    onActivated: function(index) { root.selectedNavigationPage = index }
                }
                PreferenceNavRow {
                    objectName: "uiPrimitivesPreferenceNavEdit"
                    Layout.fillWidth: true
                    pageIndex: 1
                    title: "Edit"
                    subtitle: "Adjust the selected photograph"
                    showSubtitle: false
                    selected: root.selectedNavigationPage === pageIndex
                    onActivated: function(index) { root.selectedNavigationPage = index }
                }
                PreferenceNavRow {
                    objectName: "uiPrimitivesPreferenceNavExport"
                    Layout.fillWidth: true
                    pageIndex: 2
                    title: "Export"
                    subtitle: "Prepare a delivery"
                    showSubtitle: false
                    selected: root.selectedNavigationPage === pageIndex
                    onActivated: function(index) { root.selectedNavigationPage = index }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: theme.hairlineWidth; color: theme.lineSoft }
                Label {
                    text: "Sources & folders"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    font.weight: theme.weightControl
                }
                ImportSourceRow {
                    objectName: "uiPrimitivesImportSource"
                    Layout.fillWidth: true
                    label: "Pictures"
                    subtitle: "Photo library"
                    path: "sample://pictures"
                    sourceUrl: "sample://pictures"
                    selected: !root.sampleFolderSelected
                    onActivated: function(path, sourceUrl, label) {
                        root.sampleFolderSelected = false
                        root.lastOpenedLocation = path
                    }
                }
                ImportFolderRow {
                    objectName: "uiPrimitivesImportFolder"
                    Layout.fillWidth: true
                    fileName: "Reference photos"
                    fileUrl: "sample://reference"
                    selected: root.sampleFolderSelected
                    onActivated: function(fileUrl) {
                        root.sampleFolderSelected = true
                        root.lastOpenedLocation = fileUrl
                    }
                }
                Label {
                    objectName: "uiPrimitivesNavigationRoleGuidance"
                    Layout.fillWidth: true
                    text: root.lastOpenedLocation.length > 0
                          ? "Opened local sample · " + root.lastOpenedLocation
                          : "Persistent destinations use selected surfaces. Source and folder activation here only changes the local sample."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }
        }

        StudioPanel {
            id: statusPanel
            objectName: "uiPrimitivesFeedbackStatusPanel"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: statusContent.implicitHeight + theme.typeSection + (theme.space12 * 4)
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            title: "Status"
            subhead: "StatusChip · settled or compact state"

            ColumnLayout {
                id: statusContent
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space8

                Flow {
                    Layout.fillWidth: true
                    spacing: theme.space6

                    StatusChip { objectName: "uiPrimitivesStatusNeutral"; text: "Ready"; tone: "neutral" }
                    StatusChip { objectName: "uiPrimitivesStatusAccent"; text: "Selected"; tone: "accent" }
                    StatusChip { objectName: "uiPrimitivesStatusInfo"; text: "Synced"; tone: "info" }
                    StatusChip { objectName: "uiPrimitivesStatusSuccess"; text: "Complete"; tone: "success" }
                    StatusChip { objectName: "uiPrimitivesStatusWarning"; text: "Review"; tone: "warning" }
                    StatusChip { objectName: "uiPrimitivesStatusError"; text: "Failed"; tone: "error" }
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    StatusChip {
                        objectName: "uiPrimitivesStatusBusy"
                        text: root.sampleBusy ? "Analyzing" : "Paused"
                        detailText: "Local state toggle"
                        tone: root.sampleFailed ? "error" : "info"
                        busy: root.sampleBusy
                        pulse: !theme.reducedMotion
                        actionText: root.sampleFailed ? "Retry" : ""
                        actionEnabled: true
                        onActionTriggered: {
                            root.retryCount += 1
                            root.sampleFailed = false
                            root.sampleBusy = true
                        }
                    }

                    ActionButton {
                        objectName: "uiPrimitivesToggleStatusButton"
                        text: root.sampleBusy ? "Pause sample" : "Resume sample"
                        compact: true
                        onClicked: root.sampleBusy = !root.sampleBusy
                    }

                    ActionButton {
                        objectName: "uiPrimitivesToggleFailureButton"
                        text: root.sampleFailed ? "Clear failure" : "Show failure"
                        compact: true
                        onClicked: {
                            root.sampleFailed = !root.sampleFailed
                            root.sampleBusy = false
                        }
                    }
                }

                Label {
                    objectName: "uiPrimitivesStatusRoleGuidance"
                    Layout.fillWidth: true
                    text: "Role: concise state that remains visible near its subject. Use warning/error tones for attention, not decoration. Retries belong on recoverable failures."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: theme.gapPanel

        StudioPanel {
            id: activityPanel
            objectName: "uiPrimitivesFeedbackActivityPanel"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: activityContent.implicitHeight + theme.typeSection + (theme.space12 * 4)
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            title: "Activity"
            subhead: "Banner · status bar · progress"

            ColumnLayout {
                id: activityContent
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space10

                ActivityBanner {
                    objectName: "uiPrimitivesActivityBanner"
                    Layout.fillWidth: true
                    title: root.sampleFailed ? "Analysis needs attention" : "Analyzing selected media"
                    detail: root.retryCount > 0
                            ? "Gallery-only example · retry count " + root.retryCount
                            : "Gallery-only example · no job or backend attached"
                    progressValue: root.sampleFailed ? -1 : root.sampleBusy ? root.sampleProgress : -1
                    accent: root.sampleFailed ? theme.error : theme.accentCool400
                    emphasized: root.sampleFailed

                    ActionButton {
                        objectName: "uiPrimitivesActivityAction"
                        visible: root.sampleFailed
                        text: "Retry"
                        iconName: "retry"
                        compact: true
                        onClicked: {
                            root.retryCount += 1
                            root.sampleFailed = false
                            root.sampleBusy = true
                        }
                    }
                }

                ActivityStatusBar {
                    objectName: "uiPrimitivesActivityStatusBar"
                    Layout.fillWidth: true
                    visible: true
                    title: root.sampleFailed ? "Action failed" : root.sampleBusy ? "Working" : "Paused"
                    detail: root.sampleFailed ? "Retry is available" : "Local status example"
                    progressValue: root.sampleFailed ? -1 : root.sampleBusy ? root.sampleProgress : -1
                    failed: root.sampleFailed
                    retryable: root.sampleFailed
                    cancelable: root.sampleBusy
                    onRetryRequested: {
                        root.retryCount += 1
                        root.sampleFailed = false
                        root.sampleBusy = true
                    }
                    onCancelRequested: root.sampleBusy = false
                }

                ProgressStrip {
                    objectName: "uiPrimitivesProgressStrip"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 5
                    progress: root.sampleFailed ? -1 : root.sampleBusy ? root.sampleProgress : 1
                    failed: root.sampleFailed
                    accent: theme.accentCool400
                }

                InlineActivityIndicator {
                    objectName: "uiPrimitivesInlineActivityIndicator"
                    Layout.fillWidth: true
                    title: "Thumbnail cache"
                    detail: root.sampleBusy ? "Updating · illustrative state" : "Current · illustrative state"
                    progressValue: root.sampleBusy ? root.sampleProgress : 1
                    accent: theme.accentCool400
                    cancelable: root.sampleBusy
                    onCancelRequested: root.sampleBusy = false
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8

                    ActionButton {
                        objectName: "uiPrimitivesProgressStepButton"
                        text: "Advance progress"
                        compact: true
                        onClicked: {
                            root.sampleProgress = root.sampleProgress >= 1 ? 0.12 : Math.min(1, root.sampleProgress + 0.13)
                            root.sampleBusy = true
                            root.sampleFailed = false
                        }
                    }

                    Label {
                        objectName: "uiPrimitivesProgressValue"
                        Layout.fillWidth: true
                        text: root.sampleFailed ? "Failure" : root.sampleBusy ? Math.round(root.sampleProgress * 100) + "%" : "Indeterminate / paused"
                        color: theme.textMuted
                        font.family: theme.fontMono
                        font.pixelSize: theme.typeCaption
                    }
                }

                Label {
                    objectName: "uiPrimitivesActivityRoleGuidance"
                    Layout.fillWidth: true
                    text: "Role: use a banner for a task with detail or actions; an integrated status bar for brief app-wide work. Indeterminate means no reliable estimate."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }
        }

        StudioPanel {
            id: informationPanel
            objectName: "uiPrimitivesFeedbackInformationPanel"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            implicitHeight: informationContent.implicitHeight + theme.typeSection + (theme.space12 * 4)
            Layout.minimumHeight: implicitHeight
            Layout.preferredHeight: implicitHeight
            title: "Information surfaces"
            subhead: "StudioPanel · compact values · empty state"

            ColumnLayout {
                id: informationContent
                anchors.fill: parent
                anchors.margins: theme.space12
                spacing: theme.space10

                RowLayout {
                    Layout.fillWidth: true
                    spacing: theme.space8
                    InfoPill { objectName: "uiPrimitivesInfoPillFormat"; label: "FORMAT"; value: "RAW" }
                    InfoPill { objectName: "uiPrimitivesInfoPillCount"; label: "ITEMS"; value: "24" }
                }

                StudioMetadataRow {
                    objectName: "uiPrimitivesStudioMetadataRow"
                    label: "Camera"
                    value: "Looksy Field Camera · selectable read-only value"
                }

                StudioEmptyStateCard {
                    objectName: "uiPrimitivesStudioEmptyState"
                    Layout.fillWidth: true
                    Layout.preferredHeight: 154
                    studioTheme: theme
                    hairline: theme.alpha(theme.lineSoft, 0.24)
                    accentColor: theme.accentCool400
                    dimText: theme.textMuted
                    iconName: "media"
                    title: "No items in this view"
                    message: "State the reason and the next available action. This sample stays inert."
                }

                Label {
                    objectName: "uiPrimitivesSurfaceRoleGuidance"
                    Layout.fillWidth: true
                    text: "Role: panels organize related controls; metadata rows support scanning and text selection; pills summarize one or two values. Empty states should explain cause and recovery."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }

                Label {
                    objectName: "uiPrimitivesFeedbackOpenDecisions"
                    Layout.fillWidth: true
                    text: "Open decisions: when should task progress move from inline status to a persistent activity card? Which metadata values deserve a compact summary versus a selectable row?"
                    color: theme.textFaint
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }
        }
    }
}
