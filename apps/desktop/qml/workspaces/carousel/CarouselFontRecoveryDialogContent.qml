pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../../components"

ColumnLayout {
    id: content
    required property var root
    required property var theme
    required property var carouselDocument
    required property var carouselFontRecoveryDialog
    readonly property bool hasPreview: carouselFontRecoveryDialog.previewEntries.length > 0
    readonly property var missingFamilies: {
        const entries = carouselFontRecoveryDialog.missingEntries;
        const families = [];
        for (let index = 0; index < entries.length; ++index) {
            const entry = entries[index];
            if (!families.some(function(family) { return family.fontFamily.toLowerCase() === String(entry.fontFamily).toLowerCase(); }))
                families.push({ fontFamily: String(entry.fontFamily), label: String(entry.resolvedFontFamily || entry.fontFamily) });
        }
        return families;
    }
    readonly property bool previewMatchesSelection: hasPreview
        && String(carouselFontRecoveryDialog.previewEntries[0].sourceFontFamily).toLowerCase() === String(missingFontCombo.currentValue || "").toLowerCase()
        && String(carouselFontRecoveryDialog.previewEntries[0].replacementFontFamily).toLowerCase() === replacementFontCombo.currentText.toLowerCase()
    readonly property int overflowCount: carouselFontRecoveryDialog.previewEntries.filter(function(entry) { return Boolean(entry.overflowAfter); }).length
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function focusInitialTarget() {
        if (hasPreview) {
            const entry = carouselFontRecoveryDialog.previewEntries[0];
            for (let index = 0; index < missingFamilies.length; ++index) {
                if (missingFamilies[index].fontFamily.toLowerCase() === String(entry.sourceFontFamily).toLowerCase()) {
                    missingFontCombo.currentIndex = index;
                    break;
                }
            }
            for (let index = 0; index < content.carouselDocument.installedFontFamilies.length; ++index) {
                if (String(content.carouselDocument.installedFontFamilies[index]).toLowerCase() === String(entry.replacementFontFamily).toLowerCase()) {
                    replacementFontCombo.currentIndex = index;
                    break;
                }
            }
            applyButton.forceActiveFocus(Qt.TabFocusReason);
        } else {
            missingFontCombo.forceActiveFocus(Qt.TabFocusReason);
        }
    }
    function invalidateChangedPreview() {
        if (hasPreview && !previewMatchesSelection)
            content.carouselDocument.clearFontSubstitutionPreview();
    }

    RowLayout {
        objectName: "carouselFontRecoveryHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Resolve fonts"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }
        ActionButton {
            objectName: "carouselFontRecoveryCloseButton"
            compact: true
            text: "Close"
            iconName: "close"
            onClicked: content.carouselFontRecoveryDialog.close()
        }
    }
    Label {
        Layout.fillWidth: true
        text: "Export stays blocked until each unavailable face is installed or an explicit replacement is accepted after reviewing text heights."
        color: content.theme.textMuted
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    GridLayout {
        objectName: "carouselFontRecoveryChoices"
        Layout.fillWidth: true
        columns: width >= content.theme.scaled(420) ? 2 : 1
        columnSpacing: content.theme.space12
        rowSpacing: content.theme.space8
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            spacing: content.theme.space4
            Label {
                text: "Missing face"
                color: content.theme.textMuted
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeCaption
            }
            ToneComboBox {
                id: missingFontCombo
                objectName: "carouselMissingFontCombo"
                accessibleLabel: "Missing font to replace"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: content.missingFamilies
                textRole: "label"
                valueRole: "fontFamily"
                enabled: count > 0
                onActivated: content.invalidateChangedPreview()
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            spacing: content.theme.space4
            Label {
                text: "Installed replacement"
                color: content.theme.textMuted
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeCaption
            }
            ToneComboBox {
                id: replacementFontCombo
                objectName: "carouselReplacementFontCombo"
                accessibleLabel: "Installed replacement font"
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                model: content.carouselDocument.installedFontFamilies
                onActivated: content.invalidateChangedPreview()
            }
        }
    }
    RowLayout {
        objectName: "carouselFontRecoverySummary"
        Layout.fillWidth: true
        spacing: content.theme.space12
        ActionButton {
            objectName: "carouselPreviewFontReplacementButton"
            text: "Preview"
            compact: true
            enabled: missingFontCombo.currentIndex >= 0 && replacementFontCombo.currentIndex >= 0
            disabledReason: "Choose a missing face and an installed replacement."
            onClicked: content.carouselDocument.previewFontSubstitution(
                String(missingFontCombo.currentValue || ""), replacementFontCombo.currentText)
        }
        Label {
            objectName: "carouselFontRecoveryChangeCount"
            Layout.fillWidth: true
            text: {
                const count = content.hasPreview ? content.carouselFontRecoveryDialog.previewEntries.length : content.carouselFontRecoveryDialog.missingEntries.length;
                return count + " text layer" + (count === 1 ? "" : "s")
                    + (content.hasPreview ? " reviewed" : count === 1 ? " needs a font" : " need a font")
                    + (content.overflowCount > 0 ? " · " + content.overflowCount + " overflow" : "");
            }
            color: content.overflowCount > 0 ? content.theme.error : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    ListView {
        id: recoveryList
        objectName: "carouselFontRecoveryList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: Math.min(content.theme.scaled(360), Math.max(content.theme.scaled(140), contentHeight))
        clip: true
        spacing: content.theme.space12
        model: content.hasPreview ? content.carouselFontRecoveryDialog.previewEntries : content.carouselFontRecoveryDialog.missingEntries
        reuseItems: true
        activeFocusOnTab: true
        function focusRow(index) {
            if (count === 0)
                return;
            currentIndex = Math.max(0, Math.min(index, count - 1));
            positionViewAtIndex(currentIndex, ListView.Contain);
            forceActiveFocus(Qt.TabFocusReason);
        }
        Keys.onDownPressed: focusRow(currentIndex + 1)
        Keys.onUpPressed: focusRow(currentIndex < 0 ? 0 : currentIndex - 1)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                focusRow(event.key === Qt.Key_Home ? 0 : count - 1);
                event.accepted = true;
            }
        }
        highlightMoveDuration: content.theme.durationFast
        highlightResizeDuration: 0
        highlight: Rectangle {
            visible: recoveryList.activeFocus
            radius: content.theme.radiusControl
            color: content.theme.alpha(content.theme.focusFill, 0.14)
            border.width: content.theme.focusBorderWidth
            border.color: content.theme.focusRing
        }
        Accessible.role: Accessible.List
        Accessible.name: "Affected text layers and layout changes"
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            active: true
            visible: recoveryList.count > 0 && size < 1
        }
        delegate: Item {
            id: recoveryRow
            required property var modelData
            required property int index
            objectName: "carouselFontRecoveryRow_" + index
            width: recoveryList.width - content.theme.space8
            implicitHeight: recoveryRowDetails.implicitHeight + content.theme.space8 * 2
            Accessible.role: Accessible.ListItem
            Accessible.name: "Board " + (Number(modelData.boardIndex || 0) + 1) + ", " + String(modelData.layerLabel || "Text layer")
            Accessible.selectable: true
            Accessible.selected: recoveryList.currentIndex === index
            Accessible.onPressAction: recoveryList.focusRow(index)
            TapHandler { onTapped: recoveryList.focusRow(recoveryRow.index) }
            ColumnLayout {
                id: recoveryRowDetails
                anchors.fill: parent
                anchors.margins: content.theme.space8
                spacing: content.theme.space6
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 1
                    color: content.theme.alpha(content.theme.lineSoft, 0.3)
                }
                Label {
                    objectName: "carouselFontRecoveryLayer_" + recoveryRow.index
                    Layout.fillWidth: true
                    text: "Board " + (Number(recoveryRow.modelData.boardIndex || 0) + 1) + " · " + String(recoveryRow.modelData.layerLabel || "Text layer")
                    color: content.theme.textStrong
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeControl
                    font.weight: Font.DemiBold
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    Layout.fillWidth: true
                    visible: !content.hasPreview
                    text: "Missing: " + String(recoveryRow.modelData.resolvedFontFamily || recoveryRow.modelData.fontFamily)
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    objectName: "carouselFontRecoveryMetrics_" + recoveryRow.index
                    Layout.fillWidth: true
                    visible: content.hasPreview
                    text: "Text height: " + Number(recoveryRow.modelData.beforeHeight || 0).toFixed(1) + " → "
                          + Number(recoveryRow.modelData.afterHeight || 0).toFixed(1) + " px · Frame: "
                          + Number(recoveryRow.modelData.frameHeight || 0).toFixed(1) + " px"
                    color: content.theme.textMuted
                    font.family: content.theme.fontMono
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    objectName: "carouselFontRecoveryStatus_" + recoveryRow.index
                    Layout.fillWidth: true
                    visible: content.hasPreview
                    text: Boolean(recoveryRow.modelData.overflowAfter) ? "OVERFLOW · Replacement exceeds the text frame."
                          : Boolean(recoveryRow.modelData.layoutChanged) ? "Text height changes."
                          : "Text height unchanged."
                    color: Boolean(recoveryRow.modelData.overflowAfter) ? content.theme.error
                           : Boolean(recoveryRow.modelData.layoutChanged) ? content.theme.warning : content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
            }
        }
        Label {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - content.theme.space20 * 2)
            visible: recoveryList.count === 0
            text: "All text layers use available fonts."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeBody
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }
    }
    RowLayout {
        objectName: "carouselFontRecoveryFooter"
        Layout.fillWidth: true
        spacing: content.theme.space8
        ActionButton {
            objectName: "carouselOpenFontBookButton"
            text: "Open Font Book"
            compact: true
            onClicked: content.carouselDocument.openFontManagement()
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            objectName: "carouselFontRecoveryCancelButton"
            text: "Cancel"
            compact: true
            onClicked: content.carouselFontRecoveryDialog.close()
        }
        ActionButton {
            id: applyButton
            objectName: "carouselApplyFontReplacementButton"
            text: "Apply replacement"
            compact: true
            tone: "primary"
            enabled: content.previewMatchesSelection
            disabledReason: "Preview the selected font pair's layout before applying its replacement."
            tooltipText: "Apply the previewed substitution to " + content.carouselFontRecoveryDialog.previewEntries.length
                + " text layer" + (content.carouselFontRecoveryDialog.previewEntries.length === 1 ? "" : "s")
            onClicked: {
                if (content.carouselDocument.applyFontSubstitution())
                    content.carouselFontRecoveryDialog.close();
            }
        }
    }
}
