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
    required property var carouselLayoutRecipeDialog
    readonly property var preview: carouselLayoutRecipeDialog.preview
    readonly property bool hasPreview: String(preview.recipeId || "").length > 0
    readonly property bool currentPreview: hasPreview && Number(preview.boardIndex) === carouselDocument.currentIndex &&
                                           String(preview.documentHash) === carouselDocument.documentContentHash
    readonly property var allRows: comparisonRows()
    readonly property var changedRows: allRows.filter(row => row.changed)
    readonly property var rows: showUnchanged.checked ? allRows : changedRows
    readonly property bool compactComparison: width < theme.scaled(650)
    readonly property bool canApply: Boolean(preview.canApply) && currentPreview && changedRows.length > 0
    readonly property string guidance: !hasPreview ? "Preview a saved recipe to review its settings."
        : !currentPreview ? "The document changed after this preview. Refresh to review the current layout."
        : !changedRows.length ? "This recipe already matches the current layout."
        : "One Undo restores these layout settings."
    property string selectedKey: ""
    property string actionError: ""
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function number(value) { return Number(value).toFixed(2).replace(/\.?0+$/, ""); }
    function pair(values) { return number(values[0]) + " / " + number(values[1]) + " px"; }
    function guide(value) {
        return value ? (value.orientation === "vertical" ? "Vertical" : "Horizontal") +
                       " #" + Number(value.guideId) + " · " + number(value.position) + " px" : "None";
    }
    function comparisonRows() {
        if (!hasPreview) return [];
        const before = preview.beforeSettings || {};
        const after = preview.afterSettings || {};
        const beforeBoard = preview.beforeBoard || {};
        const afterBoard = preview.afterBoard || {};
        const result = [];
        function add(key, label, oldValue, newValue, format) {
            result.push({ key: key, label: label, before: format(oldValue), after: format(newValue),
                          changed: JSON.stringify(oldValue) !== JSON.stringify(newValue) });
        }
        const grid = values => Number(values[0]) + " × " + Number(values[1]);
        for (const scope of [{ key: "board", label: "Board", before: beforeBoard, after: afterBoard },
                             { key: "defaults", label: "Document default", before: before, after: after }]) {
            add(scope.key + "-grid", scope.label + " grid", [scope.before.columns, scope.before.rows], [scope.after.columns, scope.after.rows], grid);
            add(scope.key + "-margins", scope.label + " margins (X / Y)", [scope.before.marginX, scope.before.marginY], [scope.after.marginX, scope.after.marginY], pair);
            add(scope.key + "-gutters", scope.label + " gutters (X / Y)", [scope.before.gutterX, scope.before.gutterY], [scope.after.gutterX, scope.after.gutterY], pair);
        }
        const switches = [
            ["gridEnabled", "Grid overlay"], ["guidesEnabled", "Custom guide overlay"], ["rulersEnabled", "Rulers"],
            ["thirdsEnabled", "Rule of thirds"], ["safeEnabled", "Safe-area overlay"], ["bleedEnabled", "Bleed overlay"],
            ["snappingEnabled", "Snapping"], ["snapToGrid", "Snap to grid"], ["aspectLocked", "Lock aspect ratio"]
        ];
        for (const setting of switches) add(setting[0], setting[1], before[setting[0]], after[setting[0]], value => value ? "On" : "Off");
        const presets = { "custom": "Custom", "instagram-feed": "Instagram Feed", "reels-stories": "Reels / Stories", "tiktok": "TikTok", "device-ui": "Device UI" };
        add("safePreset", "Safe-area preset", before.safePreset, after.safePreset, value => presets[value] || String(value));
        add("safeInset", "Safe inset", before.safeInset, after.safeInset, value => number(Number(value) * 100) + "%");
        for (const setting of [["bleed", "Bleed"], ["snapThreshold", "Snap distance"], ["nudgeStep", "Nudge step"], ["largeNudgeStep", "Large nudge step"]])
            add(setting[0], setting[1], before[setting[0]], after[setting[0]], value => number(value) + " px");
        add("guideOpacity", "Guide opacity", before.guideOpacity, after.guideOpacity, value => number(Number(value) * 100) + "%");
        add("guideColor", "Guide color", before.guideColor, after.guideColor, value => String(value));
        const oldGuides = before.customGuides || [];
        const newGuides = after.customGuides || [];
        for (let index = 0; index < Math.max(oldGuides.length, newGuides.length); ++index)
            add("guide-" + index, "Guide " + (index + 1), oldGuides[index], newGuides[index], guide);
        return result;
    }
    function choose(index, focusList) {
        if (!rows.length) { selectedKey = ""; comparison.currentIndex = -1; return; }
        const row = Math.max(0, Math.min(index, rows.length - 1));
        selectedKey = String(rows[row].key);
        comparison.currentIndex = row;
        comparison.positionViewAtIndex(row, ListView.Contain);
        if (focusList) comparison.forceActiveFocus(Qt.TabFocusReason);
    }
    function restoreSelection() {
        for (let index = 0; index < rows.length; ++index)
            if (rows[index].key === selectedKey) { choose(index, false); return; }
        choose(0, false);
    }
    onRowsChanged: Qt.callLater(restoreSelection)
    function focusInitialTarget() {
        actionError = "";
        nameFlickable.contentY = 0;
        showUnchanged.checked = false;
        choose(0, false);
        if (rows.length) comparison.forceActiveFocus(Qt.TabFocusReason);
        else closeControl.forceActiveFocus(Qt.TabFocusReason);
    }

    RowLayout {
        objectName: "carouselLayoutRecipeHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Layout recipe"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        ActionButton {
            id: closeControl
            objectName: "carouselLayoutRecipeCloseButton"
            text: "Close"
            iconName: "close"
            compact: true
            onClicked: content.carouselLayoutRecipeDialog.close()
        }
    }
    ColumnLayout {
        objectName: "carouselLayoutRecipeContext"
        Layout.fillWidth: true
        spacing: content.theme.space6
        ScrollView {
            id: nameArea
            objectName: "carouselLayoutRecipeNameScroll"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(content.theme.scaled(60), recipeName.implicitHeight + content.theme.space8)
            padding: content.theme.space4
            contentWidth: availableWidth
            contentHeight: recipeName.implicitHeight
            clip: true
            activeFocusOnTab: contentHeight > availableHeight
            Accessible.name: "Recipe name"
            ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
            background: Rectangle {
                color: "transparent"
                radius: content.theme.radiusControl
                border.width: nameArea.activeFocus ? content.theme.focusBorderWidth : 0
                border.color: content.theme.focusRing
            }
            Keys.onDownPressed: nameFlickable.contentY = Math.min(Math.max(0, contentHeight - availableHeight), nameFlickable.contentY + content.theme.scaled(24))
            Keys.onUpPressed: nameFlickable.contentY = Math.max(0, nameFlickable.contentY - content.theme.scaled(24))
            Keys.onPressed: event => {
                if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                    nameFlickable.contentY = event.key === Qt.Key_Home ? 0 : Math.max(0, contentHeight - availableHeight);
                    event.accepted = true;
                }
            }
            contentItem: Flickable {
                id: nameFlickable
                contentWidth: width
                contentHeight: recipeName.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                Label {
                    id: recipeName
                    objectName: "carouselLayoutRecipeName"
                    width: nameFlickable.width
                    height: implicitHeight
                    text: content.hasPreview ? String(content.preview.name) : "No recipe selected"
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeControl
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: (content.hasPreview ? "Board " + Number(content.preview.boardNumber) + " geometry" : "Board geometry") + " and document-wide precision settings. Artwork and styling stay unchanged."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    RowLayout {
        objectName: "carouselLayoutRecipeSummary"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            objectName: "carouselLayoutRecipeDifferenceCount"
            Layout.fillWidth: true
            text: content.changedRows.length + " differences · " + Number(content.preview.guideCount || 0) + " guides in recipe"
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        ToneCheckBox {
            id: showUnchanged
            objectName: "carouselLayoutRecipeShowUnchangedToggle"
            Layout.maximumWidth: content.width / 2
            visible: content.hasPreview
            text: "Show unchanged (" + (content.allRows.length - content.changedRows.length) + ")"
            allowLabelWrap: true
        }
    }
    RowLayout {
        objectName: "carouselLayoutRecipeColumnHeader"
        Layout.fillWidth: true
        Layout.leftMargin: content.theme.space8
        Layout.rightMargin: content.theme.space8 * 2
        visible: !content.compactComparison && content.rows.length > 0
        spacing: content.theme.space8
        Label { Layout.preferredWidth: comparison.width * 0.3 - content.theme.space8; text: "Setting"; color: content.theme.textMuted; font.family: content.theme.fontUi; font.pixelSize: content.theme.typeCaption }
        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 1; text: "Current"; color: content.theme.textMuted; font.family: content.theme.fontUi; font.pixelSize: content.theme.typeCaption }
        Label { Layout.fillWidth: true; Layout.minimumWidth: 0; Layout.preferredWidth: 1; text: "Recipe"; color: content.theme.textMuted; font.family: content.theme.fontUi; font.pixelSize: content.theme.typeCaption }
    }
    ListView {
        id: comparison
        objectName: "carouselLayoutRecipeComparison"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: content.theme.scaled(340)
        model: content.rows
        spacing: content.theme.space4
        clip: true
        reuseItems: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        boundsBehavior: Flickable.StopAtBounds
        Accessible.role: Accessible.List
        Accessible.name: "Layout settings comparison"
        onHeightChanged: Qt.callLater(content.restoreSelection)
        Keys.onDownPressed: content.choose(currentIndex + 1, true)
        Keys.onUpPressed: content.choose(currentIndex - 1, true)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                content.choose(event.key === Qt.Key_Home ? 0 : count - 1, true);
                event.accepted = true;
            }
        }
        ScrollBar.vertical: ScrollBar {
            objectName: "carouselLayoutRecipeScrollBar"
            policy: ScrollBar.AsNeeded
            active: true
            visible: comparison.count > 0 && comparison.contentHeight > comparison.height
        }
        delegate: Rectangle {
            id: row
            required property var modelData
            required property int index
            objectName: "carouselLayoutRecipeRow_" + modelData.key
            width: comparison.width - content.theme.space8
            height: Math.max(content.theme.scaled(44), rowContents.implicitHeight) + content.theme.space8 * 2
            onHeightChanged: {
                if (content.selectedKey === String(modelData.key)) Qt.callLater(content.restoreSelection);
            }
            radius: content.theme.radiusControl
            color: content.theme.alpha(content.theme.focusFill, modelData.changed ? 0.05 : 0)
            border.width: content.selectedKey === modelData.key && comparison.activeFocus ? content.theme.focusBorderWidth : 0
            border.color: content.theme.focusRing
            Accessible.role: Accessible.ListItem
            Accessible.name: modelData.label + ". Current: " + modelData.before + ". Recipe: " + modelData.after + (modelData.changed ? ". Changes." : ". Unchanged.")
            Accessible.selectable: true
            Accessible.selected: content.selectedKey === modelData.key
            Accessible.onPressAction: content.choose(index, true)
            TapHandler { onTapped: content.choose(row.index, true) }
            ColumnLayout {
                id: rowContents
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: content.theme.space8
                spacing: content.theme.space4
                Label {
                    Layout.fillWidth: true
                    visible: content.compactComparison
                    text: row.modelData.label + (row.modelData.changed ? " · changes" : "")
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: content.theme.space8
                    Label {
                        Layout.preferredWidth: comparison.width * 0.3 - content.theme.space8
                        visible: !content.compactComparison
                        text: row.modelData.label + (row.modelData.changed ? " · changes" : "")
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeCaption
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        spacing: content.theme.space4
                        Label {
                            visible: content.compactComparison
                            text: "Current"
                            color: content.theme.textMuted
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                        }
                        Label {
                            objectName: "carouselLayoutRecipeBefore_" + row.modelData.key
                            Layout.fillWidth: true
                            text: row.modelData.before
                            color: content.theme.textMuted
                            font.family: content.theme.fontMono
                            font.pixelSize: content.theme.typeControl
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.preferredWidth: 1
                        spacing: content.theme.space4
                        Label {
                            visible: content.compactComparison
                            text: "Recipe"
                            color: content.theme.textMuted
                            font.family: content.theme.fontUi
                            font.pixelSize: content.theme.typeCaption
                        }
                        Label {
                            objectName: "carouselLayoutRecipeAfter_" + row.modelData.key
                            Layout.fillWidth: true
                            text: row.modelData.after
                            color: content.theme.textMain
                            font.family: content.theme.fontMono
                            font.pixelSize: content.theme.typeControl
                            font.weight: row.modelData.changed ? Font.DemiBold : Font.Normal
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                            elide: Text.ElideNone
                        }
                    }
                }
            }
        }
        Label {
            anchors.centerIn: parent
            width: parent.width - content.theme.space16
            visible: comparison.count === 0
            text: content.hasPreview ? "Show unchanged settings to inspect the full recipe." : "No layout settings to compare."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeControl
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    RowLayout {
        objectName: "carouselLayoutRecipeFooter"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            objectName: "carouselLayoutRecipeGuidance"
            Layout.fillWidth: true
            text: content.actionError.length ? content.actionError : content.guidance
            color: content.actionError.length || (content.hasPreview && !content.currentPreview) ? content.theme.warning : content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        ActionButton {
            objectName: "carouselLayoutRecipeRefreshButton"
            visible: content.hasPreview && !content.currentPreview
            text: "Refresh preview"
            compact: true
            onClicked: {
                if (content.carouselDocument.previewLayoutRecipe(String(content.preview.recipeId))) content.focusInitialTarget();
                else content.actionError = content.carouselDocument.status;
            }
        }
        ActionButton {
            objectName: "carouselApplyLayoutRecipeButton"
            text: "Apply layout"
            tone: "primary"
            enabled: content.canApply
            disabledReason: content.guidance
            onClicked: {
                if (content.carouselDocument.applyPreviewedLayoutRecipe()) content.carouselLayoutRecipeDialog.close();
                else content.actionError = content.carouselDocument.status;
            }
        }
    }
}
