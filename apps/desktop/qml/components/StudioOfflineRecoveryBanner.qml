import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

PopupSurface {
    id: banner

    required property var studioTheme
    required property string pageName
    required property string selectedAssetId
    required property string availabilityState
    required property string availabilityTitle
    required property string availabilityDetail
    required property bool importModeOpen
    required property bool missingConfirmationArmed
    required property color accentColor
    required property color dimTextColor

    signal relinkSelectedRequested()
    signal relinkAllRequested()
    signal recheckRequested()
    signal markMissingRequested()

    readonly property string recoverySurface: pageName
    readonly property string recoveryState: availabilityState
    readonly property bool exportSurface: pageName === "export"
    readonly property string preservationContract: exportSurface
                                                   ? "offline-selected-original:v1|surface:export|metadata:visible|cached-preview:visible|recipe:preserved|identity:preserved|cleanup:explicit-confirmation-only"
                                                   : "offline-selected-original:v1|metadata:visible|cached-preview:visible|recipe:preserved|identity:preserved|cleanup:explicit-confirmation-only"

    objectName: exportSurface ? "exportOfflineRecoveryBanner" : "offlineRecoveryBanner"
    visible: selectedAssetId.length > 0 &&
             availabilityState !== "online" &&
             availabilityState !== "none" &&
             !importModeOpen
    width: Math.min(banner.studioTheme.scaled(760), Math.max(0, parent ? parent.width - banner.studioTheme.space16 * 2 : banner.studioTheme.scaled(460)))
    height: recoveryContent.implicitHeight + banner.studioTheme.popupPadding * 2

    ColumnLayout {
        id: recoveryContent
        anchors.fill: parent
        anchors.margins: banner.studioTheme.popupPadding
        spacing: banner.studioTheme.space8

        RowLayout {
            Layout.fillWidth: true
            spacing: banner.studioTheme.space8

            Rectangle {
                Layout.preferredWidth: 8
                Layout.preferredHeight: 8
                radius: width / 2
                color: banner.studioTheme.warning
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                BodyLabel {
                    objectName: banner.exportSurface
                                ? "exportOfflineRecoveryTitle" : "offlineRecoveryTitle"
                    Layout.fillWidth: true
                    text: banner.availabilityTitle
                    color: banner.studioTheme.textStrong
                    font.family: banner.studioTheme.fontUi
                    font.pixelSize: banner.studioTheme.typeControl
                    font.weight: Font.DemiBold
                    elide: Text.ElideNone
                }

                BodyLabel {
                    objectName: banner.exportSurface
                                ? "exportOfflineRecoveryDetail" : "offlineRecoveryDetail"
                    Layout.fillWidth: true
                    text: banner.availabilityDetail
                    color: banner.dimTextColor
                    font.family: banner.studioTheme.fontUi
                    font.pixelSize: banner.studioTheme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                }
            }

            BodyLabel {
                objectName: banner.exportSurface
                            ? "exportOfflineRecoverySurfaceLabel" : "offlineRecoverySurfaceLabel"
                text: banner.pageName === "library" ? "LIBRARY"
                      : banner.pageName === "edit" ? "EDIT" : "EXPORT"
                color: banner.accentColor
                font.family: banner.studioTheme.fontUi
                font.pixelSize: banner.studioTheme.typeMicro
                font.weight: Font.DemiBold
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: banner.studioTheme.space8

            ActionButton {
                objectName: banner.exportSurface
                            ? "exportOfflineRelinkSelectedButton" : "offlineRelinkSelectedButton"
                implicitWidth: Math.max(banner.studioTheme.scaled(132), commandNaturalWidth)
                implicitHeight: banner.studioTheme.controlHeight
                minimumReadableWidth: banner.studioTheme.scaled(132)
                text: "Relink This"
                iconName: "folder"
                accent: banner.accentColor
                compact: true
                tooltipText: "Choose the folder containing this original."
                onClicked: banner.relinkSelectedRequested()
            }

            MiniButton {
                objectName: banner.exportSurface
                            ? "exportOfflineRelinkAllButton" : "offlineRelinkAllButton"
                text: "Relink All"
                iconName: "folder"
                role: "secondary"
                tooltip: "Match all offline originals by filename in one folder tree."
                onClicked: banner.relinkAllRequested()
            }

            MiniButton {
                objectName: banner.exportSurface
                            ? "exportOfflineRecheckButton" : "offlineRecheckButton"
                text: "Recheck"
                iconName: "refresh"
                role: "secondary"
                tooltip: "Check whether the original or source drive is available again."
                onClicked: banner.recheckRequested()
            }


            MiniButton {
                objectName: banner.exportSurface
                            ? "exportOfflineMarkMissingButton" : "offlineMarkMissingButton"
                text: banner.missingConfirmationArmed ? "Confirm Missing" : "Mark Missing"
                iconName: banner.missingConfirmationArmed ? "warning" : "menu"
                role: "ghost"
                accent: banner.missingConfirmationArmed
                        ? banner.studioTheme.warning : banner.dimTextColor
                tooltip: banner.missingConfirmationArmed
                         ? "Confirm that the original is permanently missing. The catalog entry and edits remain."
                         : "Explicitly mark this original missing without deleting its catalog entry or edits."
                onClicked: banner.markMissingRequested()
            }
        }
    }
}
