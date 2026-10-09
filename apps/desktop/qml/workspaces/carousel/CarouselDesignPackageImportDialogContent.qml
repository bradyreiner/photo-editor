pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia
import QtQml
import "../../components"

ColumnLayout {
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselDesignPackageImportDialog
    spacing: 0

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 18
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Label {
                Layout.fillWidth: true
                text: "Import design package"
                color: theme.textStrong
                font.family: theme.fontDisplay
                font.pixelSize: 21
                font.weight: Font.Bold
                elide: Text.ElideNone
            }
            Label {
                Layout.fillWidth: true
                text: String(carouselDocument.designPackageImportPreview.name || "Design artifact")
                      + " · "
                      + String(carouselDocument.designPackageImportPreview.artifactType || "package")
                color: theme.textMuted
                font.family: theme.fontUi
                font.pixelSize: 10
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
        }
        StatusChip {
            text: Boolean(carouselDocument.designPackageImportPreview.checksumValid)
                  ? "Checksums valid" : "Checksum failed"
            tone: Boolean(carouselDocument.designPackageImportPreview.checksumValid)
                  ? "success" : "danger"
            compact: true
        }
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 1
        color: theme.alpha(theme.lineSoft, 0.4)
    }

    ScrollView {
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 9
            Label {
                Layout.fillWidth: true
                Layout.margins: 18
                text: "Package v" + Number(carouselDocument.designPackageImportPreview.packageVersion || 0)
                      + " · " + Number(carouselDocument.designPackageImportPreview.assetCount || 0)
                      + " manifested assets · "
                      + Number(carouselDocument.designPackageImportPreview.embeddedAssetCount || 0)
                      + " embedded brand assets"
                color: theme.textStrong
                font.family: theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WordWrap
                elide: Text.ElideNone
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                implicitHeight: excludedMediaLabel.implicitHeight + 16
                radius: 8
                color: theme.alpha(theme.accentWarm400, 0.08)
                border.width: 1
                border.color: theme.alpha(theme.accentWarm400, 0.4)
                Label {
                    id: excludedMediaLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    text: Number(carouselDocument.designPackageImportPreview.excludedPrivateMediaCount || 0)
                          + " private project media assets were intentionally not embedded. Imported component media remains unresolved until explicitly relinked."
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: 10
                    wrapMode: Text.WordWrap
                    elide: Text.ElideNone
                }
            }
            Rectangle {
                objectName: "carouselDesignPackageConflictReview"
                Layout.fillWidth: true
                Layout.leftMargin: 18
                Layout.rightMargin: 18
                implicitHeight: conflictReviewColumn.implicitHeight + 16
                radius: 8
                color: Boolean(carouselDocument.designPackageImportPreview.hasConflict)
                       ? theme.alpha(theme.warning, 0.10)
                       : theme.alpha(theme.success, 0.08)
                border.width: 1
                border.color: Boolean(carouselDocument.designPackageImportPreview.hasConflict)
                              ? theme.alpha(theme.warning, 0.5)
                              : theme.alpha(theme.success, 0.4)
                ColumnLayout {
                    id: conflictReviewColumn
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    Label {
                        Layout.fillWidth: true
                        text: Boolean(carouselDocument.designPackageImportPreview.hasConflict)
                              ? "Conflict review required" : "No library conflict"
                        color: theme.textStrong
                        font.family: theme.fontUi
                        font.pixelSize: 11
                        font.weight: Font.DemiBold
                        elide: Text.ElideNone
                    }
                    Label {
                        Layout.fillWidth: true
                        text: (Boolean(carouselDocument.designPackageImportPreview.idConflict)
                               ? "The artifact ID already exists. " : "")
                              + (Boolean(carouselDocument.designPackageImportPreview.nameConflict)
                                 ? "The artifact name already exists. " : "")
                              + "Keep Both creates a new identity; Replace uses the reviewed package. Nothing changes until you choose."
                        color: theme.textMuted
                        font.family: theme.fontUi
                        font.pixelSize: 9
                        wrapMode: Text.WordWrap
                        elide: Text.ElideNone
                    }
                }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.margins: 14
        spacing: 7
        ActionButton {
            Layout.fillWidth: true
            text: "Cancel"
            onClicked: {
                carouselDocument.clearDesignPackageImportPreview()
                carouselDesignPackageImportDialog.close()
            }
        }
        ActionButton {
            objectName: "carouselImportPackageKeepBothButton"
            Layout.fillWidth: true
            text: "Keep Both"
            enabled: Boolean(carouselDocument.designPackageImportPreview.canImport)
            onClicked: {
                if (carouselDocument.applyDesignPackageImport("keep-both"))
                    carouselDesignPackageImportDialog.close()
            }
        }
        ActionButton {
            objectName: "carouselImportPackageReplaceButton"
            Layout.fillWidth: true
            visible: Boolean(carouselDocument.designPackageImportPreview.hasConflict)
            text: "Replace"
            tone: "primary"
            enabled: Boolean(carouselDocument.designPackageImportPreview.canImport)
            onClicked: {
                if (carouselDocument.applyDesignPackageImport("replace"))
                    carouselDesignPackageImportDialog.close()
            }
        }
    }
}
