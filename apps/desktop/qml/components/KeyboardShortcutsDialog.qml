pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: 860
    height: 620
    padding: 0
    standardButtons: Dialog.NoButton

    readonly property bool macStyle: Qt.platform.os === "osx"
    readonly property string primaryLabel: macStyle ? "Cmd" : "Ctrl"
    readonly property string redoLabel: macStyle ? "Cmd+Shift+Z" : "Ctrl+Y"
    readonly property var shortcutSections: [
        {
            title: "Global",
            accent: theme.accentCool400,
            items: [
                { "keys": primary("O"), "description": "Import a folder into the library." },
                { "keys": primary("N"), "description": "Create a new timeline." },
                { "keys": primary("Shift+E"), "description": "Queue the current selection for export." },
                { "keys": primary(","), "description": "Open preferences." }
            ]
        },
        {
            title: "Edit",
            accent: theme.accentWarm400,
            items: [
                { "keys": primary("Z"), "description": "Undo the last photo adjustment." },
                { "keys": redoLabel, "description": "Redo the most recently undone adjustment." },
                { "keys": primary("D"), "description": "Deselect the active mask and clear transient selection tools." },
                { "keys": "Esc", "description": "Exit the current mask tool or deselect the active mask." },
                { "keys": "Left", "description": "Move to the previous photo in the filmstrip." },
                { "keys": "Right", "description": "Move to the next photo in the filmstrip." }
            ]
        },
        {
            title: "Timeline",
            accent: theme.warning,
            items: [
                { "keys": primary("S"), "description": "Save the current timeline." },
                { "keys": "Space", "description": "Play or pause sequence playback." },
                { "keys": "Left / Right", "description": "Step back / forward one frame." },
                { "keys": "Home / End", "description": "Jump to start / end of timeline." },
                { "keys": "V", "description": "Select tool." },
                { "keys": "B", "description": "Razor tool." },
                { "keys": "Y", "description": "Slip tool." },
                { "keys": "N", "description": "Roll tool." },
                { "keys": "S", "description": "Toggle snapping." },
                { "keys": "M", "description": "Drop a sequence marker at the playhead." },
                { "keys": primary("= / -"), "description": "Zoom in / out." },
                { "keys": primary("0"), "description": "Zoom to fit all clips." },
                { "keys": primary("\\"), "description": "Split the selected clip at the playhead." },
                { "keys": "Delete / Backspace", "description": "Delete selected clip." },
                { "keys": primary("Shift+M"), "description": "Open the media browser." },
                { "keys": primary("Shift+T"), "description": "Add a video track." }
            ]
        },
        {
            title: "Carousel",
            accent: theme.success,
            items: [
                { "keys": primary("Shift+A"), "description": "Add a new slide." },
                { "keys": primary("["), "description": "Move the current slide earlier." },
                { "keys": primary("]"), "description": "Move the current slide later." },
                { "keys": primary("Shift+C"), "description": "Export the active carousel." }
            ]
        }
    ]

    Theme { id: theme }

    function primary(key) {
        return primaryLabel + "+" + key
    }

    parent: Overlay.overlay
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0

    Overlay.modal: Rectangle {
        color: theme.alpha(theme.ink950, 0.78)
    }

    background: Rectangle {
        radius: theme.radiusPanel + 6
        color: theme.ink900
        border.width: 1
        border.color: theme.alpha(theme.lineSoft, 0.78)

        gradient: Gradient {
            GradientStop { position: 0.0; color: theme.alpha(theme.ink700, 0.12) }
            GradientStop { position: 0.08; color: theme.alpha(theme.ink850, 0.98) }
            GradientStop { position: 1.0; color: theme.ink950 }
        }
    }

    contentItem: ColumnLayout {
        anchors.fill: parent
        anchors.margins: theme.space18
        spacing: theme.space16

        RowLayout {
            Layout.fillWidth: true
            spacing: theme.space16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: theme.space6

                Label {
                    Layout.fillWidth: true
                    text: "Keyboard Shortcuts"
                    color: theme.textStrong
                    font.family: theme.fontDisplay
                    font.pixelSize: 28
                    font.weight: Font.DemiBold
                }

                Label {
                    Layout.fillWidth: true
                    text: macStyle
                          ? "Custom shortcuts use Command on macOS, alongside native shortcuts like Command+Comma for Preferences."
                          : "Custom shortcuts use Control on desktop platforms, alongside native shortcuts like Control+Comma for Preferences."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            ActionButton {
                text: "Close"
                compact: true
                onClicked: root.close()
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: Math.max(0, parent.width)
                spacing: theme.space16

                Repeater {
                    model: root.shortcutSections

                    delegate: Rectangle {
                        required property var modelData

                        Layout.fillWidth: true
                        radius: theme.radiusPanel
                        color: theme.alpha(theme.ink850, 0.96)
                        border.width: 1
                        border.color: theme.alpha(modelData.accent, 0.38)
                        implicitHeight: body.implicitHeight + theme.panelPadding * 2

                        gradient: Gradient {
                            GradientStop { position: 0.0; color: theme.alpha(modelData.accent, 0.08) }
                            GradientStop { position: 0.18; color: theme.alpha(theme.ink850, 0.98) }
                            GradientStop { position: 1.0; color: theme.ink900 }
                        }

                        ColumnLayout {
                            id: body
                            anchors.fill: parent
                            anchors.margins: theme.panelPadding
                            spacing: theme.space12

                            Label {
                                Layout.fillWidth: true
                                text: modelData.title
                                color: theme.textStrong
                                font.family: theme.fontUi
                                font.pixelSize: 16
                                font.weight: Font.DemiBold
                            }

                            Repeater {
                                model: modelData.items

                                delegate: Rectangle {
                                    required property var modelData

                                    Layout.fillWidth: true
                                    radius: theme.radiusControl
                                    color: theme.alpha(theme.ink900, 0.82)
                                    border.width: 1
                                    border.color: theme.alpha(theme.lineSoft, 0.74)
                                    implicitHeight: shortcutRow.implicitHeight + theme.space12 * 2

                                    RowLayout {
                                        id: shortcutRow
                                        anchors.fill: parent
                                        anchors.margins: theme.space12
                                        spacing: theme.space12

                                        Rectangle {
                                            Layout.alignment: Qt.AlignTop
                                            radius: 7
                                            color: theme.alpha(theme.ink750, 0.92)
                                            border.width: 1
                                            border.color: theme.alpha(theme.lineSoft, 0.82)
                                            implicitWidth: keyLabel.implicitWidth + 18
                                            implicitHeight: keyLabel.implicitHeight + 12

                                            Label {
                                                id: keyLabel
                                                anchors.centerIn: parent
                                                text: modelData.keys
                                                color: theme.textStrong
                                                font.family: theme.fontMono
                                                font.pixelSize: 12
                                                font.weight: Font.DemiBold
                                            }
                                        }

                                        Label {
                                            Layout.fillWidth: true
                                            text: modelData.description
                                            color: theme.textMain
                                            font.family: theme.fontUi
                                            font.pixelSize: 12
                                            wrapMode: Text.WordWrap
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
