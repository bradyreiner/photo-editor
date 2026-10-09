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
    required property var carouselRecoveryDialog
    anchors.fill: parent
    anchors.margins: 20
    spacing: 12

    Label {
        Layout.fillWidth: true
        text: "Recover Carousel"
        color: theme.textStrong
        font.family: theme.fontDisplay
        font.pixelSize: 22
        font.weight: Font.DemiBold
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: "A crash-safe snapshot exists for “" + String(carouselRecoveryDialog.recovery.documentName || "Untitled Carousel") + "”."
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 12
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    Label {
        Layout.fillWidth: true
        text: (carouselRecoveryDialog.recovery.usingLastGood ? "Using last good snapshot  " : "Snapshot  ") +
              String(carouselRecoveryDialog.recovery.capturedAt || "time unavailable")
        color: carouselRecoveryDialog.recovery.usingLastGood ? theme.warning : theme.textFaint
        font.family: theme.fontMono
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        elide: Text.ElideNone
    }

    Item { Layout.fillHeight: true }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        ActionButton {
            text: "Discard"
            tone: "danger"
            compact: true
            onClicked: {
                carouselDocument.discardRecovery()
                carouselRecoveryDialog.close()
                if (!carouselDocument.hasActiveCarousel)
                    root.beginCarouselSetup()
            }
        }

        Item { Layout.fillWidth: true }

        ActionButton {
            text: "Open Saved Version"
            compact: true
            visible: Boolean(carouselRecoveryDialog.recovery.hasCurrentSavedVersion)
            onClicked: {
                if (carouselDocument.openCurrentSavedVersion())
                    carouselRecoveryDialog.close()
            }
        }

        ActionButton {
            text: "Open As Copy"
            compact: true
            onClicked: {
                if (carouselDocument.openRecoveryCopy())
                    carouselRecoveryDialog.close()
            }
        }

        ActionButton {
            text: "Restore"
            tone: "primary"
            compact: true
            onClicked: {
                if (carouselDocument.restoreRecovery())
                    carouselRecoveryDialog.close()
            }
        }
    }
}
