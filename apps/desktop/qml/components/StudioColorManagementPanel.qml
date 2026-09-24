import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import QtQuick.Layouts

ColumnLayout {
    id: panel

    required property var editorPreview
    required property var theme
    property color dimText: "#8a8a8a"
    property string proofingStatusText: ""
    property string iccProfileDialogSlot: "preview"
    property bool iccProfileDialogRelink: false
    property bool dcpProfileDialogRelink: false

    Layout.fillWidth: true
    spacing: 8

    FileDialog {
        id: iccProfileDialog
        title: (panel.iccProfileDialogRelink ? "Relink " : "Choose ") +
               panel.iccProfileDialogSlot + " ICC Profile"
        nameFilters: ["ICC profiles (*.icc *.icm)", "All files (*)"]
        onAccepted: {
            if (panel.iccProfileDialogRelink)
                panel.editorPreview.relinkIccProfileAsset(panel.iccProfileDialogSlot, selectedFile)
            else
                panel.editorPreview.importIccProfileAsset(panel.iccProfileDialogSlot, selectedFile)
        }
    }

    FileDialog {
        id: dcpProfileDialog
        title: panel.dcpProfileDialogRelink ? "Relink Camera DCP Profile"
                                            : "Choose Camera DCP Profile"
        nameFilters: ["DCP camera profiles (*.dcp)", "All files (*)"]
        onAccepted: {
            if (panel.dcpProfileDialogRelink)
                panel.editorPreview.relinkDcpProfileAsset(selectedFile)
            else
                panel.editorPreview.importDcpProfileAsset(selectedFile)
        }
    }

    function openIccProfileDialog(slot, relink) {
        panel.iccProfileDialogSlot = String(slot || "preview")
        panel.iccProfileDialogRelink = Boolean(relink)
        iccProfileDialog.open()
    }

    function openDcpProfileDialog(relink) {
        panel.dcpProfileDialogRelink = Boolean(relink)
        dcpProfileDialog.open()
    }

    StudioRowRule {}
    StudioSectionTitle { label: "Preview & Proof" }

    Label {
        Layout.fillWidth: true
        text: "Preview target"
        color: panel.dimText
        font.pixelSize: 10
    }

    ToneComboBox {
        objectName: "studioPreviewOutputModeCombo"
        Layout.fillWidth: true
        model: panel.editorPreview.previewOutputModeOptions
        currentIndex: Math.max(0, model.indexOf(panel.editorPreview.previewOutputMode))
        onActivated: panel.editorPreview.previewOutputMode = currentText
    }

    ToneComboBox {
        objectName: "studioReferenceDisplayTargetCombo"
        Layout.fillWidth: true
        visible: panel.editorPreview.previewOutputMode === "Reference Display Target"
        model: ["sRGB", "Display P3", "Adobe RGB", "ProPhoto RGB", "Rec.709", "Rec.2020"]
        currentIndex: Math.max(0, model.indexOf(panel.editorPreview.referenceDisplayTarget))
        onActivated: panel.editorPreview.referenceDisplayTarget = currentText
    }

    Label {
        Layout.fillWidth: true
        text: panel.editorPreview.previewOutputMode === "Monitor"
              ? "Final display conversion: " + String(panel.editorPreview.systemMonitorProfileName || panel.editorPreview.monitorColorProfile || "System monitor")
              : panel.editorPreview.previewOutputMode === "Match Export"
                ? "Previewing export target: " + panel.editorPreview.outputColorSpace
                : panel.editorPreview.previewOutputMode === "Soft Proof"
                  ? panel.proofingStatusText
                  : "Reference display: " + panel.editorPreview.referenceDisplayTarget
        color: panel.dimText
        font.family: panel.theme.fontMono
        font.pixelSize: 9
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        MiniButton {
            objectName: "studioSoftProofToggle"
            Layout.preferredWidth: 96
            text: panel.editorPreview.softProofEnabled ? "Proof On" : "Proof Off"
            role: "segmented"
            active: panel.editorPreview.softProofEnabled
            enabled: panel.editorPreview.hasAsset
            onClicked: panel.editorPreview.softProofEnabled = !panel.editorPreview.softProofEnabled
        }

        ToneComboBox {
            objectName: "studioSoftProofProfileCombo"
            Layout.fillWidth: true
            model: ["sRGB", "Display P3", "Adobe RGB", "ProPhoto RGB", "Rec.709", "Rec.2020"]
            currentIndex: Math.max(0, model.indexOf(panel.editorPreview.softProofProfile))
            enabled: panel.editorPreview.softProofEnabled &&
                     !((panel.editorPreview.iccProfileAssets || ({})).proof || ({})).configured
            onActivated: panel.editorPreview.softProofProfile = currentText
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: panel.editorPreview.softProofEnabled
        spacing: 6

        ToneComboBox {
            objectName: "studioProofRenderIntentCombo"
            Layout.fillWidth: true
            model: ["Relative Colorimetric", "Perceptual"]
            currentIndex: Math.max(0, model.indexOf(panel.editorPreview.proofRenderIntent))
            enabled: !panel.editorPreview.proofSimulatePaperWhite
            onActivated: panel.editorPreview.proofRenderIntent = currentText
        }

        MiniButton {
            objectName: "studioProofPaperToggle"
            text: "Paper"
            role: "segmented"
            active: panel.editorPreview.proofSimulatePaperWhite
            tooltip: "Use absolute colorimetric proofing to simulate the proof medium white."
            onClicked: panel.editorPreview.proofSimulatePaperWhite =
                       !panel.editorPreview.proofSimulatePaperWhite
        }

        MiniButton {
            objectName: "studioProofInkToggle"
            text: "Ink"
            role: "segmented"
            active: panel.editorPreview.proofSimulateInkBlack
            tooltip: "Simulate the proof device black without black-point compensation."
            onClicked: panel.editorPreview.proofSimulateInkBlack =
                       !panel.editorPreview.proofSimulateInkBlack
        }
    }

    Label {
        Layout.fillWidth: true
        visible: panel.editorPreview.softProofEnabled
        text: {
            const diagnostics = panel.editorPreview.colorTransformDiagnostics || ({})
            const processor = diagnostics.proofProcessor || "unavailable"
            const profile = diagnostics.proofProfileName || panel.editorPreview.softProofProfile
            return "Proof: " + profile + " · " + processor
        }
        color: (panel.editorPreview.colorTransformDiagnostics || ({})).proofProcessor ===
               "Little CMS three-profile soft proof"
               ? panel.theme.success : panel.theme.error
        font.family: panel.theme.fontMono
        font.pixelSize: 9
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }

    StudioRowRule {}
    StudioSectionTitle { label: "Profile Assets" }

    Label {
        Layout.fillWidth: true
        text: "Profiles are content-hashed. Missing or changed bytes stop rendering instead of silently falling back."
        color: panel.dimText
        font.pixelSize: 10
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }

    Repeater {
        model: [
            { slot: "input", label: "Source" },
            { slot: "preview", label: "Display" },
            { slot: "proof", label: "Proof" },
            { slot: "export", label: "Export" }
        ]

        ColumnLayout {
            id: iccProfileEntry
            required property var modelData
            readonly property var profileAsset:
                (panel.editorPreview.iccProfileAssets || ({}))[iccProfileEntry.modelData.slot] || ({})
            Layout.fillWidth: true
            spacing: 3

            RowLayout {
                Layout.fillWidth: true
                spacing: 5
                Label { Layout.preferredWidth: 48; text: iccProfileEntry.modelData.label; color: panel.dimText; font.family: panel.theme.fontMono; font.pixelSize: 9 }
                Label {
                    Layout.fillWidth: true
                    text: iccProfileEntry.profileAsset.configured
                          ? (iccProfileEntry.profileAsset.displayName || iccProfileEntry.profileAsset.assetId)
                          : "Named default"
                    color: iccProfileEntry.profileAsset.executionReady || !iccProfileEntry.profileAsset.configured
                           ? panel.theme.textMain : panel.theme.error
                    font.pixelSize: 10
                    wrapMode: Text.WrapAnywhere
                    maximumLineCount: 2
                    elide: Text.ElideNone
                }
                MiniButton { text: iccProfileEntry.profileAsset.configured ? "Replace" : "Choose"; role: "secondary"; enabled: iccProfileEntry.profileAsset.executionSupported !== false; onClicked: panel.openIccProfileDialog(iccProfileEntry.modelData.slot, false) }
                MiniButton { text: "Relink"; role: "secondary"; visible: iccProfileEntry.profileAsset.configured; onClicked: panel.openIccProfileDialog(iccProfileEntry.modelData.slot, true) }
                MiniButton { text: "Clear"; role: "secondary"; visible: iccProfileEntry.profileAsset.configured; onClicked: panel.editorPreview.clearIccProfileAsset(iccProfileEntry.modelData.slot) }
            }

            Label {
                Layout.fillWidth: true
                visible: iccProfileEntry.profileAsset.configured || iccProfileEntry.profileAsset.executionSupported === false
                text: iccProfileEntry.profileAsset.executionReady
                      ? "Verified · " + String(iccProfileEntry.profileAsset.payloadSha256 || "").slice(0, 12)
                      : (iccProfileEntry.profileAsset.executionFailureReason || iccProfileEntry.profileAsset.failureReason || iccProfileEntry.profileAsset.status)
                color: iccProfileEntry.profileAsset.executionReady ? panel.theme.success : panel.theme.error
                font.family: panel.theme.fontMono
                font.pixelSize: 9
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
        }
    }

    ColumnLayout {
        id: dcpProfileEntry
        readonly property var profileAsset: panel.editorPreview.cameraDcpProfileAsset || ({})
        Layout.fillWidth: true
        visible: panel.editorPreview.rawControlsAvailable
        spacing: 3

        RowLayout {
            Layout.fillWidth: true
            spacing: 5
            Label { Layout.preferredWidth: 48; text: "DCP"; color: panel.dimText; font.family: panel.theme.fontMono; font.pixelSize: 9 }
            Label {
                Layout.fillWidth: true
                text: dcpProfileEntry.profileAsset.configured
                      ? (dcpProfileEntry.profileAsset.profileName || dcpProfileEntry.profileAsset.displayName || dcpProfileEntry.profileAsset.assetId)
                      : "No custom camera profile"
                color: dcpProfileEntry.profileAsset.executionReady || !dcpProfileEntry.profileAsset.configured
                       ? panel.theme.textMain : panel.theme.error
                font.pixelSize: 10
                wrapMode: Text.WrapAnywhere
                maximumLineCount: 2
                elide: Text.ElideNone
            }
            MiniButton { text: dcpProfileEntry.profileAsset.configured ? "Replace" : "Choose"; role: "secondary"; enabled: dcpProfileEntry.profileAsset.executionSupported !== false; onClicked: panel.openDcpProfileDialog(false) }
            MiniButton { text: "Relink"; role: "secondary"; visible: dcpProfileEntry.profileAsset.configured; onClicked: panel.openDcpProfileDialog(true) }
            MiniButton { text: "Clear"; role: "secondary"; visible: dcpProfileEntry.profileAsset.configured; onClicked: panel.editorPreview.clearDcpProfileAsset() }
        }

        Label {
            Layout.fillWidth: true
            visible: dcpProfileEntry.profileAsset.configured || dcpProfileEntry.profileAsset.executionSupported === false
            text: dcpProfileEntry.profileAsset.executionReady
                  ? "Verified · " + String(dcpProfileEntry.profileAsset.payloadSha256 || "").slice(0, 12) + " · " + (dcpProfileEntry.profileAsset.cameraCompatibility || "unknown")
                  : (dcpProfileEntry.profileAsset.executionFailureReason || dcpProfileEntry.profileAsset.failureReason || dcpProfileEntry.profileAsset.status)
            color: dcpProfileEntry.profileAsset.executionReady ? panel.theme.success : panel.theme.error
            font.family: panel.theme.fontMono
            font.pixelSize: 9
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 2
            elide: Text.ElideNone
        }
    }
}
