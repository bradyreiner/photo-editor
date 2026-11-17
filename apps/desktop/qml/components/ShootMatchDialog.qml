pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

AppPopup {
    id: root
    required property var catalog
    property var assetIds: []
    property string referenceId: ""
    property var checkedIds: []
    property var syncCurrentEdit: null
    property int page: 0
    readonly property var review: catalog ? catalog.shootMatchState : ({})
    readonly property bool running: catalog && catalog.shootMatchBusy
    readonly property var referenceChoices: assetIds.map(function(id) {
        const asset = root.catalog.assetById(id);
        return {id: id, name: String(asset.filePath || id).split(/[\\/]/).pop()};
    })
    readonly property bool applying: review.state === "applying"
    property Item focusReturnItem: null
    objectName: "shootMatchDialog"
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    width: Math.min(popupAvailableWidth, theme.scaled(780))
    height: Math.min(popupAvailableHeight, theme.scaled(page === 0 ? 510 : 640))
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 2 : 0
    Theme { id: theme }

    function toggleTarget(id) {
        const next = checkedIds.slice();
        const index = next.indexOf(id);
        if (index < 0) next.push(id);
        else next.splice(index, 1);
        checkedIds = next;
    }
    function buildPreviews() {
        if (syncCurrentEdit && !syncCurrentEdit()) return;
        checkedIds = [];
        catalog.prepareShootMatch(referenceId, assetIds.filter(id => id !== referenceId), {
            lighting: lighting.checked, look: look.checked,
            lightingAmount: lightingAmount.value / 100, lookAmount: lookAmount.value / 100
        });
    }
    function applyReviewed() {
        if (syncCurrentEdit && !syncCurrentEdit()) return;
        catalog.applyReviewedShootMatch(checkedIds);
    }
    function signed(value, digits) {
        const number = Number(value || 0);
        return (number >= 0 ? "+" : "") + number.toFixed(digits);
    }
    Connections {
        target: root.catalog
        function onShootMatchChanged() {
            if (!root.visible) return;
            if (root.review.state === "review" && root.page === 0) {
                root.checkedIds = (root.review.results || []).filter(row => row.recommended).map(row => row.assetId);
                root.page = 1;
                Qt.callLater(function() { applyButton.forceActiveFocus(Qt.TabFocusReason); });
            }
        }
    }
    onAboutToShow: {
        page = 0;
        checkedIds = [];
        catalog.clearShootMatch();
        referenceBox.currentIndex = Math.max(0, referenceChoices.findIndex(row => row.id === referenceId));
        const popupWindow = parent ? parent.Window.window : null;
        focusReturnItem = popupWindow ? popupWindow.activeFocusItem : null;
    }
    onOpened: referenceBox.forceActiveFocus(Qt.TabFocusReason)
    onClosed: {
        if (running) catalog.cancelShootMatch();
        Qt.callLater(function() {
            if (root.focusReturnItem && root.focusReturnItem.visible && root.focusReturnItem.enabled)
                root.focusReturnItem.forceActiveFocus(Qt.TabFocusReason);
            root.focusReturnItem = null;
        });
    }
    contentItem: ColumnLayout {
        spacing: theme.space12
        Label {
            Layout.fillWidth: true
            text: root.page === 0 ? qsTr("Match selected photos") : qsTr("Review reference matches")
            color: theme.textStrong
            font.family: theme.fontUi
            font.pixelSize: theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        Label {
            Layout.fillWidth: true
            text: root.page === 0
                  ? qsTr("Choose a reference, then build editable proposals for the other selected photos.")
                  : qsTr("Compare each proposal with its original. Outliers start unchecked. Distance measures image statistics; it is not a quality rating.")
            color: theme.textMuted
            font.family: theme.fontUi
            font.pixelSize: theme.typeBody
            wrapMode: Text.Wrap
        }
        ScrollView {
            id: optionsScroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.page === 0
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: optionsScroll.availableWidth
                spacing: theme.space12
                Label {
                    text: qsTr("Reference photo")
                    color: theme.textMain
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeControl
                }
                ToneComboBox {
                    id: referenceBox
                    objectName: "shootMatchReferencePicker"
                    Layout.fillWidth: true
                    model: root.referenceChoices
                    textRole: "name"
                    enabled: !root.running
                    Accessible.name: qsTr("Reference photo")
                    onActivated: root.referenceId = root.referenceChoices[currentIndex].id
                }
                ToneCheckBox {
                    id: lighting
                    objectName: "shootMatchLightingToggle"
                    text: qsTr("Lighting · exposure and white balance")
                    checked: true
                    enabled: !root.running
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: lighting.checked && !root.running
                    Slider {
                        id: lightingAmount
                        objectName: "shootMatchLightingAmount"
                        Layout.fillWidth: true
                        from: 0; to: 100; value: 100; stepSize: 1
                        Accessible.name: qsTr("Lighting amount")
                    }
                    Label { text: Math.round(lightingAmount.value) + "%"; color: theme.textMain; font.family: theme.fontNumeric }
                }
                ToneCheckBox {
                    id: look
                    objectName: "shootMatchLookToggle"
                    text: qsTr("Look · contrast and saturation")
                    checked: true
                    enabled: !root.running
                }
                RowLayout {
                    Layout.fillWidth: true
                    enabled: look.checked && !root.running
                    Slider {
                        id: lookAmount
                        objectName: "shootMatchLookAmount"
                        Layout.fillWidth: true
                        from: 0; to: 100; value: 75; stepSize: 1
                        Accessible.name: qsTr("Look amount")
                    }
                    Label { text: Math.round(lookAmount.value) + "%"; color: theme.textMain; font.family: theme.fontNumeric }
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("Masks, layers, crop, retouching and profile settings stay editable. Review up to 64 target photos per run.")
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }
        }
        RowLayout {
            visible: root.page === 1
            Layout.fillWidth: true
            Image {
                objectName: "shootMatchReferencePreview"
                Layout.preferredWidth: theme.scaled(70)
                Layout.preferredHeight: theme.scaled(50)
                source: root.review.referenceSource || ""
                fillMode: Image.PreserveAspectFit
                asynchronous: true
            }
            Label {
                Layout.fillWidth: true
                text: qsTr("Reference · ") + String(root.review.referenceName || "")
                color: theme.textStrong
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                wrapMode: Text.Wrap
            }
        }
        ListView {
            id: results
            objectName: "shootMatchReviewList"
            visible: root.page === 1
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: theme.space12
            model: root.review.results || []
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
            delegate: ColumnLayout {
                id: row
                required property var modelData
                width: results.width - theme.space12
                spacing: theme.space4
                ToneCheckBox {
                    objectName: "shootMatchTarget_" + row.modelData.assetId
                    Layout.fillWidth: true
                    text: String(row.modelData.name || row.modelData.assetId)
                    checked: root.checkedIds.indexOf(row.modelData.assetId) >= 0
                    enabled: row.modelData.ok && row.modelData.changed && !root.applying && root.review.state !== "applied"
                    onClicked: root.toggleTarget(row.modelData.assetId)
                }
                RowLayout {
                    visible: row.modelData.ok === true
                    Layout.fillWidth: true
                    spacing: theme.space8
                    Repeater {
                        model: [qsTr("Before"), qsTr("Proposed")]
                        delegate: ColumnLayout {
                            required property int index
                            required property string modelData
                            Layout.fillWidth: true
                            Label { text: modelData; color: theme.textMuted; font.family: theme.fontUi; font.pixelSize: theme.typeCaption }
                            Image {
                                objectName: "shootMatchPreview_" + row.modelData.assetId + "_" + index
                                Layout.fillWidth: true
                                Layout.preferredHeight: theme.scaled(100)
                                source: index === 0 ? row.modelData.beforeSource : row.modelData.afterSource
                                fillMode: Image.PreserveAspectFit
                                asynchronous: true
                            }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    visible: row.modelData.ok === true
                    text: qsTr("Exposure ") + root.signed(row.modelData.changes ? row.modelData.changes.exposure : 0, 2)
                          + qsTr(" EV · Contrast ") + root.signed(row.modelData.changes ? row.modelData.changes.contrast : 0, 0)
                          + qsTr(" · Saturation ") + root.signed(row.modelData.changes ? row.modelData.changes.saturation : 0, 0)
                          + qsTr(" · Distance ") + Number(row.modelData.beforeDistance || 0).toFixed(2) + " → " + Number(row.modelData.afterDistance || 0).toFixed(2)
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
                Label {
                    Layout.fillWidth: true
                    visible: text.length > 0
                    text: String(row.modelData.error || row.modelData.warning || "")
                    color: theme.warning
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    wrapMode: Text.Wrap
                }
            }
        }
        RowLayout {
            visible: root.running
            Layout.fillWidth: true
            BusyIndicator { running: root.running; Layout.preferredWidth: theme.scaled(28); Layout.preferredHeight: theme.scaled(28) }
            Label {
                Layout.fillWidth: true
                text: root.review.state === "cancelling" ? qsTr("Stopping matching…") : qsTr("Building local comparison previews…")
                color: theme.textMain
                font.family: theme.fontUi
                font.pixelSize: theme.typeControl
                wrapMode: Text.Wrap
            }
        }
        Label {
            Layout.fillWidth: true
            visible: text.length > 0
            text: String(root.review.error || (root.review.state === "applied" ? qsTr("Applied to %1 photos. Undo is available in batch history.").arg(root.review.appliedCount) : ""))
            color: root.review.state === "applied" ? theme.textMain : theme.warning
            font.family: theme.fontUi
            font.pixelSize: theme.typeControl
            wrapMode: Text.Wrap
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton {
                objectName: "shootMatchCloseButton"
                text: root.running ? qsTr("Cancel matching") : qsTr("Close")
                enabled: !root.applying
                onClicked: root.running ? root.catalog.cancelShootMatch() : root.close()
            }
            ActionButton {
                visible: root.page === 1 && root.review.state !== "applied"
                text: qsTr("Adjust matching")
                enabled: !root.applying
                onClicked: root.page = 0
            }
            Item { Layout.fillWidth: true }
            ActionButton {
                id: applyButton
                objectName: "shootMatchApplyButton"
                tone: "primary"
                text: root.page === 0 ? qsTr("Build previews") : qsTr("Apply %1 photos").arg(root.checkedIds.length)
                enabled: !root.running && !root.applying && root.review.state !== "applied" &&
                    (root.page === 0 ? root.assetIds.length > 1 &&
                        (lighting.checked && lightingAmount.value > 0 || look.checked && lookAmount.value > 0) : root.checkedIds.length > 0)
                onClicked: root.page === 0 ? root.buildPreviews() : root.applyReviewed()
            }
        }
    }
}
