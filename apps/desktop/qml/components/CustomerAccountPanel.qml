pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var controller: null
    readonly property bool serviceAvailable: controller !== null && controller.configured
    spacing: theme.space16
    Theme { id: theme }

    Label {
        Layout.fillWidth: true
        text: root.controller ? root.controller.status : "Keep your purchase receipt as proof of ownership."
        color: theme.textMain
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
    }
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable && root.controller.active
        text: root.controller ? "Plan: " + root.controller.plan : ""
        color: theme.textStrong
        font.pixelSize: 18
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.serviceAvailable
        spacing: theme.space10
        ActionButton {
            text: "Start guest trial"
            enabled: root.serviceAvailable && !root.controller.busy && !root.controller.active
            onClicked: root.controller.startTrial()
        }
        ActionButton {
            text: "Refresh access"
            enabled: root.serviceAvailable && !root.controller.busy
            onClicked: root.controller.refresh()
        }
        Item { Layout.fillWidth: true }
    }
    Label {
        visible: root.serviceAvailable
        Layout.fillWidth: true
        text: "Your license key"
        color: theme.textMain
    }
    TextField {
        id: licenseKey
        objectName: "customerLicenseKey"
        visible: root.serviceAvailable
        Layout.fillWidth: true
        echoMode: TextInput.Password
        placeholderText: "Paste the key from your purchase email"
        selectByMouse: true
        maximumLength: 128
        color: theme.textStrong
        placeholderTextColor: theme.textMuted
        background: Rectangle {
            color: theme.fieldFill
            radius: theme.radiusControl
            border.width: 1
            border.color: licenseKey.activeFocus ? theme.focusRing : theme.lineSoft
        }
        Accessible.name: "License key"
        onAccepted: activateButton.clicked()
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space10
        ActionButton {
            id: activateButton
            text: "Activate purchase"
            visible: root.serviceAvailable
            enabled: root.serviceAvailable && !root.controller.busy && licenseKey.text.trim().length > 0
            onClicked: {
                if (!enabled) return
                root.controller.activate(licenseKey.text)
                licenseKey.clear()
            }
        }
        ActionButton {
            text: "Recover purchase"
            onClicked: Qt.openUrlExternally("https://app.lemonsqueezy.com/my-orders/")
        }
        Item { Layout.fillWidth: true }
    }
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable
        text: "Subscriptions and one-time licenses are supported. When editing access ends, you can still view, save, and export existing work."
        color: theme.textMuted
        wrapMode: Text.WordWrap
    }
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable
        text: "Activation sends your license key and installation identity to the license service. Photos and catalogs remain on this computer."
        color: theme.textMuted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
    }
}
