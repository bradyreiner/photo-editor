pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Popup {
    id: root

    objectName: "firstRunGuideDialog"
    property int currentStep: 0
    property bool canOpenSelectedPhoto: false
    property string errorText: ""
    readonly property int stepCount: 5
    readonly property string firstRunContract: "first-run-guide:v1|choice:sample,own-photos|path:library-cull-edit-export|nonmodal:true|skip:true|restart:true|technical-terms:hidden"
    readonly property string firstRunCompletionContract: "first-run-completion:v1|persistent-version:true|finish-explicit:true|sample:three-editable-images|own-photos:import-workspace|restart:help-menu"

    signal sampleRequested()
    signal importRequested()
    signal libraryRequested()
    signal editRequested()
    signal exportRequested()
    signal guideFinished()
    signal skipped()

    Theme { id: theme }

    modal: false
    closePolicy: Popup.NoAutoClose
    padding: 0
    width: Math.min(theme.scaled(430), parent ? parent.width - theme.scaled(32) : theme.scaled(430))
    height: Math.min(parent ? parent.height - theme.scaled(32) : theme.scaled(650),
                     Math.max(theme.scaled(550), implicitHeight))
    x: parent ? Math.max(16, parent.width - width - 22) : 0
    y: parent ? Math.min(76, Math.max(16, parent.height - height - 16)) : 0

    enter: Transition {
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: theme.durationMedium; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: root.x + 18; to: root.x; duration: theme.durationMedium; easing.type: Easing.OutCubic }
        }
    }

    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: theme.durationFast }
    }

    background: PopupSurface {
        accent: theme.accentWarm400
        fillColor: theme.alpha(theme.ink850, 0.98)
        borderColor: theme.alpha(theme.glassEdge, 0.64)
        accentRail: true
    }

    function start() {
        currentStep = 0
        errorText = ""
        open()
    }

    function chooseSample() {
        errorText = ""
        sampleRequested()
        if (errorText.length === 0)
            currentStep = 1
    }

    function chooseOwnPhotos() {
        errorText = ""
        importRequested()
        currentStep = 1
    }

    function nextStep() {
        currentStep = Math.min(stepCount - 1, currentStep + 1)
    }

    function previousStep() {
        currentStep = Math.max(0, currentStep - 1)
    }

    contentItem: ColumnLayout {
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.bottomMargin: 12
            spacing: 10

            LooksyLight {
                Layout.preferredWidth: 58
                Layout.preferredHeight: 42
                mode: root.currentStep === 0 ? "idle"
                      : root.currentStep === 1 ? "walk"
                      : root.currentStep === 2 ? "adjust"
                      : root.currentStep === 3 ? "point"
                      : "success"
                inverse: true
                accentColor: theme.primaryAccent
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                BrandWordmark {
                    visible: root.currentStep === 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: visible ? implicitHeight : 0
                    compact: true
                    showMark: false
                    showDescriptor: false
                }

                Text {
                    Layout.fillWidth: true
                    text: root.currentStep === 0 ? "Welcome"
                          : root.currentStep === 1 ? "1. Import and choose keepers"
                          : root.currentStep === 2 ? "2. Edit one keeper"
                          : root.currentStep === 3 ? "3. Export a finished photo"
                          : "Your workflow is ready"
                    color: theme.textStrong
                    font.family: theme.fontUi
                    font.pixelSize: 15
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }

                Text {
                    text: root.currentStep === 0 ? "A quick path from photos to a finished file"
                          : "Step " + String(root.currentStep) + " of 3"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                }
            }

            ActionButton {
                objectName: "firstRunSkipButton"
                text: "Skip"
                tone: "tertiary"
                compact: true
                tooltipText: "Close quick start. You can restart it from Help."
                onClicked: root.skipped()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.alpha(theme.lineSoft, 0.72)
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.margins: 18
            Layout.topMargin: 16
            Layout.bottomMargin: 16
            currentIndex: root.currentStep

            ColumnLayout {
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: "Start with your own folder, or use three included sample photos to learn the workflow without setup."
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "firstRunSampleButton"
                    Layout.fillWidth: true
                    text: "Try the sample photos"
                    iconName: "star"
                    tone: "primary"
                    minimumReadableWidth: 230
                    tooltipText: "Create and import three editable sample images."
                    onClicked: root.chooseSample()
                }

                ActionButton {
                    objectName: "firstRunOwnPhotosButton"
                    Layout.fillWidth: true
                    text: "Import my photos"
                    iconName: "folder"
                    minimumReadableWidth: 230
                    tooltipText: "Choose a folder and review it before importing."
                    onClicked: root.chooseOwnPhotos()
                }

                Text {
                    Layout.fillWidth: true
                    visible: root.errorText.length > 0
                    text: root.errorText
                    color: theme.error
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }
            }

            ColumnLayout {
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "In Library, select the photos worth keeping. Use arrow keys to move, P to pick, X to reject, and 1-5 to rate."
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }

                Text {
                    Layout.fillWidth: true
                    text: "Choose at least one keeper before continuing. Your choices can always be changed later."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "firstRunLibraryButton"
                    Layout.fillWidth: true
                    text: "Open Library"
                    iconName: "library"
                    tone: "secondary"
                    onClicked: root.libraryRequested()
                }
            }

            ColumnLayout {
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "Open a selected keeper in Edit. Start with Exposure, Contrast, and White Balance, then hold the compare control to check the original."
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "firstRunEditButton"
                    Layout.fillWidth: true
                    text: "Open selected photo in Edit"
                    iconName: "adjust"
                    tone: "primary"
                    enabled: root.canOpenSelectedPhoto
                    disabledReason: "Select a photo in Library first."
                    onClicked: root.editRequested()
                }
            }

            ColumnLayout {
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "Open Export, confirm the destination and filename preview, then start the job. Completed files can be revealed in Finder."
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "firstRunExportButton"
                    Layout.fillWidth: true
                    text: "Open Export"
                    iconName: "export"
                    tone: "primary"
                    onClicked: root.exportRequested()
                }
            }

            ColumnLayout {
                spacing: 12

                Text {
                    Layout.fillWidth: true
                    text: "You now know the core path: import, choose keepers, edit, and export. Quick Start remains available from Help whenever you want it."
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    lineHeight: 1.25
                    wrapMode: Text.Wrap
                }

                ActionButton {
                    objectName: "firstRunFinishButton"
                    Layout.fillWidth: true
                    text: "Finish Quick Start"
                    iconName: "check"
                    tone: "primary"
                    onClicked: root.guideFinished()
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: theme.alpha(theme.lineSoft, 0.72)
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 8

            ActionButton {
                text: "Back"
                compact: true
                tone: "tertiary"
                visible: root.currentStep > 0 && root.currentStep < root.stepCount - 1
                onClicked: root.previousStep()
            }

            Item { Layout.fillWidth: true }

            Row {
                spacing: 5
                Repeater {
                    model: root.stepCount
                    delegate: Rectangle {
                        required property int index
                        width: index === root.currentStep ? 18 : 6
                        height: 6
                        radius: 3
                        color: index === root.currentStep ? theme.accentWarm400 : theme.alpha(theme.textFaint, 0.45)
                        Behavior on width { NumberAnimation { duration: theme.durationFast } }
                    }
                }
            }

            Item { Layout.fillWidth: true }

            ActionButton {
                objectName: "firstRunNextButton"
                text: root.currentStep === 1 ? "Next: Edit"
                      : root.currentStep === 2 ? "Next: Export"
                      : root.currentStep === 3 ? "I exported a photo"
                      : "Next"
                compact: true
                tone: "secondary"
                visible: root.currentStep > 0 && root.currentStep < root.stepCount - 1
                onClicked: root.nextStep()
            }
        }
    }
}
