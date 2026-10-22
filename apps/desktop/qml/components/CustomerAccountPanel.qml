pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var controller: null
    property string pendingDeviceId: ""
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
        ActionButton {
            text: "Manage devices"
            enabled: root.serviceAvailable && !root.controller.busy
            onClicked: root.controller.loadDevices()
        }
        Item { Layout.fillWidth: true }
    }
    Label {
        visible: root.serviceAvailable
        Layout.fillWidth: true
        text: "Your license key"
        color: theme.textMain
    }
    ToneTextField {
        id: licenseKey
        objectName: "customerLicenseKey"
        accessibleLabel: "License key"
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
            text: "Recover access"
            objectName: "recoverCustomerAccess"
            visible: root.serviceAvailable
            enabled: root.serviceAvailable && !root.controller.busy && licenseKey.text.trim().length > 0
            onClicked: {
                if (!enabled) return
                root.controller.recoverAccess(licenseKey.text)
                licenseKey.clear()
            }
        }
        ActionButton {
            text: "Recover purchase"
            onClicked: root.controller ? root.controller.recoverPurchase() : Qt.openUrlExternally("https://app.lemonsqueezy.com/my-orders/")
        }
        Item { Layout.fillWidth: true }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.serviceAvailable && (root.controller.purchaseUrl.length > 0 ||
            root.controller.subscriptionPurchaseUrl.length > 0 || root.controller.billingPortalUrl.length > 0)
        spacing: theme.space10
        ActionButton {
            text: "Buy a license"
            objectName: "customerPurchaseLink"
            visible: root.serviceAvailable && root.controller.purchaseUrl.length > 0
            onClicked: Qt.openUrlExternally(root.controller.purchaseUrl)
        }
        ActionButton {
            text: "Subscribe"
            objectName: "customerSubscriptionLink"
            visible: root.serviceAvailable && root.controller.subscriptionPurchaseUrl.length > 0
            onClicked: Qt.openUrlExternally(root.controller.subscriptionPurchaseUrl)
        }
        ActionButton {
            text: "Manage subscription"
            objectName: "customerBillingPortalLink"
            visible: root.serviceAvailable && root.controller.billingPortalUrl.length > 0
            onClicked: Qt.openUrlExternally(root.controller.billingPortalUrl)
        }
        Item { Layout.fillWidth: true }
    }
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable && root.controller.devices.length > 0
        text: "Devices"
        color: theme.textStrong
        font.pixelSize: 16
    }
    Repeater {
        model: root.serviceAvailable ? root.controller.devices : []
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: (modelData.id === root.controller.currentInstallationId ? "This device" : modelData.platform || "Device") +
                    (modelData.revoked ? " · revoked" : " · active") +
                    (modelData.app_version ? " · " + modelData.app_version : "")
                color: theme.textMain
                elide: Text.ElideMiddle
            }
            ActionButton {
                text: "Revoke"
                visible: !modelData.revoked && modelData.id !== root.controller.currentInstallationId
                enabled: !root.controller.busy
                onClicked: {
                    root.pendingDeviceId = modelData.id
                    revokeDialog.open()
                }
            }
        }
    }
    ActionButton {
        text: "Deactivate this device"
        objectName: "deactivateCurrentDeviceAction"
        visible: root.serviceAvailable && root.controller.currentInstallationId.length > 0
        enabled: !root.controller.busy
        onClicked: deactivateDialog.open()
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
    AppDialog {
        id: revokeDialog
        objectName: "revokeCustomerDeviceDialog"
        modal: true
        title: "Revoke device access?"
        onAccepted: root.controller.revokeDevice(root.pendingDeviceId)
        header: Label {
            text: revokeDialog.title
            color: theme.textStrong
            font.pixelSize: 18
            padding: theme.space16
            background: Rectangle { color: theme.surfaceTop }
        }
        background: PopupSurface {}
        Label {
            width: 340
            text: "That device will no longer be able to refresh its license."
            color: theme.textMain
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: theme.space8
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; onClicked: revokeDialog.reject() }
            ActionButton { text: "Revoke device"; tone: "danger"; onClicked: revokeDialog.accept() }
        }
    }
    AppDialog {
        id: deactivateDialog
        objectName: "deactivateCurrentDeviceDialog"
        modal: true
        title: "Deactivate this device?"
        onAccepted: root.controller.deactivateCurrentDevice()
        header: Label {
            text: deactivateDialog.title
            color: theme.textStrong
            font.pixelSize: 18
            padding: theme.space16
            background: Rectangle { color: theme.surfaceTop }
        }
        background: PopupSurface {}
        Label {
            width: 340
            text: "This installation will be signed out and its saved access removed. You can recover it later with your verified purchase key."
            color: theme.textMain
            wrapMode: Text.WordWrap
        }
        footer: RowLayout {
            spacing: theme.space8
            Item { Layout.fillWidth: true }
            ActionButton { text: "Cancel"; onClicked: deactivateDialog.reject() }
            ActionButton { text: "Deactivate device"; tone: "danger"; onClicked: deactivateDialog.accept() }
        }
    }
}
