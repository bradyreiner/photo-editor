pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "customerAccountPanel"
    property var controller: null
    property string pendingDeviceId: ""
    property bool showPurchaseKey: false
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
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable
        text: root.serviceAvailable && root.controller.signedIn ? root.controller.accountEmail : "Sign in or create your Looksy account"
        color: theme.textStrong
        font.pixelSize: 16
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.serviceAvailable && !root.controller.signedIn && !root.controller.emailCodePending
        spacing: theme.space10
        ToneTextField {
            id: accountEmail
            objectName: "customerAccountEmail"
            Layout.fillWidth: true
            accessibleLabel: "Account email"
            placeholderText: "Your email address"
            maximumLength: 254
            inputMethodHints: Qt.ImhEmailCharactersOnly | Qt.ImhNoPredictiveText
            enabled: root.serviceAvailable && !root.controller.busy
            onAccepted: sendEmailCode.clicked()
        }
        ActionButton {
            id: sendEmailCode
            objectName: "customerSendEmailCode"
            text: "Continue with email"
            enabled: root.serviceAvailable && !root.controller.busy && accountEmail.text.trim().length > 0
            onClicked: { if (enabled) root.controller.requestEmailSignIn(accountEmail.text) }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.serviceAvailable && root.controller.emailCodePending
        spacing: theme.space10
        ToneTextField {
            id: emailCode
            objectName: "customerEmailCode"
            Layout.fillWidth: true
            accessibleLabel: "Email sign-in code"
            numericEntry: true
            placeholderText: "Eight-digit code from your email"
            maximumLength: 8
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData
            validator: RegularExpressionValidator { regularExpression: /[0-9]{0,8}/ }
            enabled: root.serviceAvailable && !root.controller.busy
            onAccepted: verifyEmailCode.clicked()
        }
        ActionButton {
            id: verifyEmailCode
            objectName: "customerVerifyEmailCode"
            text: "Sign in"
            enabled: root.serviceAvailable && !root.controller.busy && emailCode.text.length === 8
            onClicked: {
                if (!enabled) return
                root.controller.verifyEmailSignIn(emailCode.text)
                emailCode.clear()
            }
        }
        ActionButton {
            text: "Change email"
            enabled: root.serviceAvailable && !root.controller.busy
            onClicked: { emailCode.clear(); root.controller.cancelEmailSignIn() }
        }
    }
    Label {
        Layout.fillWidth: true
        visible: root.serviceAvailable && !root.controller.signedIn
        text: "Your email code also recovers your account on a new computer."
        color: theme.textMuted
        wrapMode: Text.WordWrap
    }
    RowLayout {
        Layout.fillWidth: true
        visible: root.serviceAvailable && root.controller.signedIn
        spacing: theme.space10
        ToneTextField {
            id: vipCode
            objectName: "customerVipCode"
            Layout.fillWidth: true
            accessibleLabel: "VIP access code"
            placeholderText: "VIP access code"
            maximumLength: 128
            echoMode: TextInput.Password
            enabled: root.serviceAvailable && !root.controller.busy
            onAccepted: redeemVipCode.clicked()
        }
        ActionButton {
            id: redeemVipCode
            objectName: "customerRedeemVipCode"
            text: "Redeem code"
            enabled: root.serviceAvailable && !root.controller.busy && vipCode.text.trim().length > 0
            onClicked: {
                if (!enabled) return
                root.controller.redeemVip(vipCode.text)
                vipCode.clear()
            }
        }
        ActionButton {
            objectName: "customerSignOut"
            text: "Sign out"
            enabled: root.serviceAvailable && !root.controller.busy
            onClicked: root.controller.signOut()
        }
    }
    ActionButton {
        objectName: "customerLocalSignOut"
        text: "Sign out on this computer"
        visible: root.serviceAvailable && root.controller.canSignOut && !root.controller.signedIn
        enabled: root.serviceAvailable && !root.controller.busy
        onClicked: root.controller.signOut()
    }
    Repeater {
        model: root.serviceAvailable && root.controller.signedIn ? root.controller.accountLicenses : []
        delegate: RowLayout {
            id: accountLicenseRow
            required property var modelData
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: accountLicenseRow.modelData.plan || "License"
                color: theme.textMain
                wrapMode: Text.WordWrap
            }
            ActionButton {
                text: "Use on this device"
                enabled: root.serviceAvailable && !root.controller.busy
                onClicked: root.controller.selectAccountLicense(accountLicenseRow.modelData.id)
            }
        }
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
    ActionButton {
        objectName: "customerShowPurchaseKey"
        visible: root.serviceAvailable
        text: root.showPurchaseKey ? "Hide purchase key" : "Use a purchase key"
        onClicked: root.showPurchaseKey = !root.showPurchaseKey
    }
    Label {
        visible: root.serviceAvailable && root.showPurchaseKey
        Layout.fillWidth: true
        text: "Your license key"
        color: theme.textMain
    }
    ToneTextField {
        id: licenseKey
        objectName: "customerLicenseKey"
        accessibleLabel: "License key"
        visible: root.serviceAvailable && root.showPurchaseKey
        Layout.fillWidth: true
        echoMode: TextInput.Password
        placeholderText: "Paste the key from your purchase email"
        selectByMouse: true
        maximumLength: 128
        color: theme.textStrong
        placeholderTextColor: theme.textMuted
        background: InputFieldSurface { inputControl: parent }
        onAccepted: activateButton.clicked()
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: theme.space10
        visible: root.serviceAvailable && root.showPurchaseKey
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
        visible: root.serviceAvailable && root.controller.signedIn
        spacing: theme.space10
        ActionButton {
            text: "Buy a license"
            objectName: "customerPurchaseLink"
            enabled: root.serviceAvailable && !root.controller.busy && root.controller.signedIn
            onClicked: root.controller.startCheckout("one-time")
        }
        ActionButton {
            text: "Subscribe"
            objectName: "customerSubscriptionLink"
            enabled: root.serviceAvailable && !root.controller.busy && root.controller.signedIn
            onClicked: root.controller.startCheckout("subscription")
        }
        ActionButton {
            text: "Manage subscription"
            objectName: "customerBillingPortalLink"
            enabled: root.serviceAvailable && !root.controller.busy && root.controller.signedIn
            onClicked: root.controller.openBillingPortal()
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
            id: deviceRow
            required property var modelData
            Layout.fillWidth: true
            Label {
                Layout.fillWidth: true
                text: (deviceRow.modelData.id === root.controller.currentInstallationId ? "This device" : deviceRow.modelData.platform || "Device") +
                    (deviceRow.modelData.revoked ? " · revoked" : " · active") +
                    (deviceRow.modelData.app_version ? " · " + deviceRow.modelData.app_version : "")
                color: theme.textMain
                elide: Text.ElideMiddle
            }
            ActionButton {
                text: "Revoke"
                visible: !deviceRow.modelData.revoked && deviceRow.modelData.id !== root.controller.currentInstallationId
                enabled: root.serviceAvailable && !root.controller.busy
                onClicked: {
                    root.pendingDeviceId = deviceRow.modelData.id
                    revokeDialog.open()
                }
            }
        }
    }
    ActionButton {
        text: "Deactivate this device"
        objectName: "deactivateCurrentDeviceAction"
        visible: root.serviceAvailable && root.controller.currentInstallationId.length > 0
        enabled: root.serviceAvailable && !root.controller.busy
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
        text: "Sign-in sends your email and installation identity to the account service. Purchase activation also sends your license key. Photos and catalogs remain on this computer."
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
            text: "This installation will be signed out and its saved access removed. You can recover it later by signing in or using your verified purchase key."
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
