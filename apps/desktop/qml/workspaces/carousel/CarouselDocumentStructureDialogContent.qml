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
    required property var carouselDocumentStructureDialog
    property string selectedIssueId: ""
    readonly property var issues: carouselDocumentStructureDialog.filteredIssues
    readonly property var selectedIssue: {
        for (let index = 0; index < issues.length; ++index)
            if (String(issues[index].issueId) === selectedIssueId) return issues[index];
        return ({});
    }
    anchors.fill: parent
    anchors.margins: content.theme.space18
    spacing: content.theme.space12

    function choose(index, focusList) {
        if (!issues.length) {
            selectedIssueId = "";
            findings.currentIndex = -1;
            return;
        }
        const row = Math.max(0, Math.min(index, issues.length - 1));
        selectedIssueId = String(issues[row].issueId);
        findings.currentIndex = row;
        findings.positionViewAtIndex(row, ListView.Contain);
        if (focusList) findings.forceActiveFocus(Qt.TabFocusReason);
    }
    function restoreSelection() {
        for (let index = 0; index < issues.length; ++index) {
            if (String(issues[index].issueId) === selectedIssueId) {
                choose(index, false);
                return;
            }
        }
        choose(Math.max(0, findings.currentIndex), false);
    }
    onIssuesChanged: Qt.callLater(restoreSelection)
    function focusInitialTarget() {
        restoreSelection();
        if (issues.length) findings.forceActiveFocus(Qt.TabFocusReason);
        else closeControl.forceActiveFocus(Qt.TabFocusReason);
    }

    RowLayout {
        objectName: "carouselDocumentStructureHeader"
        Layout.fillWidth: true
        spacing: content.theme.space12
        Label {
            Layout.fillWidth: true
            text: "Document structure"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
        }
        ActionButton {
            id: closeControl
            objectName: "carouselDocumentStructureCloseButton"
            text: "Close"
            iconName: "close"
            compact: true
            onClicked: content.carouselDocumentStructureDialog.close()
        }
    }
    Label {
        objectName: "carouselDocumentStructureContext"
        Layout.fillWidth: true
        text: content.carouselDocumentStructureDialog.categoryCount("all") + " findings · inspect a board or layer to correct its structure."
        color: content.theme.textMuted
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    Flow {
        objectName: "carouselDocumentStructureNavigation"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: content.theme.space6
        Repeater {
            model: [
                { label: "All", value: "all" },
                { label: "Empty slots", value: "empty-slots" },
                { label: "Duplicate copy", value: "duplicate-copy" },
                { label: "Unmatched components", value: "unmatched-components" },
                { label: "Broken links", value: "broken-links" },
                { label: "Style outliers", value: "style-outliers" }
            ]
            delegate: FilterChip {
                required property var modelData
                objectName: "carouselDocumentStructureCategory_" + modelData.value
                text: modelData.label + " " + content.carouselDocumentStructureDialog.categoryCount(modelData.value)
                active: content.root.documentStructureCategory === modelData.value
                onClicked: content.root.documentStructureCategory = modelData.value
            }
        }
    }
    ListView {
        id: findings
        objectName: "carouselDocumentStructureList"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.preferredHeight: content.theme.scaled(Math.min(360, Math.max(120, content.issues.length * 72)))
        spacing: content.theme.space4
        clip: true
        reuseItems: true
        activeFocusOnTab: true
        keyNavigationEnabled: false
        model: content.issues
        boundsBehavior: Flickable.StopAtBounds
        Accessible.name: "Structural findings"
        Accessible.role: Accessible.List
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
            objectName: "carouselDocumentStructureScrollBar"
            policy: ScrollBar.AsNeeded
            active: true
            visible: findings.count > 0 && findings.contentHeight > findings.height
        }
        delegate: Rectangle {
            id: finding
            required property var modelData
            required property int index
            objectName: "carouselDocumentStructureRow_" + index
            width: findings.width - content.theme.space8
            height: Math.max(content.theme.scaled(48), findingLabels.implicitHeight) + content.theme.space8 * 2
            radius: content.theme.radiusControl
            color: content.selectedIssueId === String(modelData.issueId) ? content.theme.alpha(content.theme.focusFill, 0.14) : "transparent"
            border.width: content.selectedIssueId === String(modelData.issueId) ? content.theme.focusBorderWidth : 0
            border.color: findings.activeFocus ? content.theme.focusRing : content.theme.lineSoft
            Accessible.role: Accessible.ListItem
            Accessible.name: (modelData.severity === "error" ? "Blocking. " : "Check. ") + String(modelData.message)
            Accessible.selectable: true
            Accessible.selected: content.selectedIssueId === String(modelData.issueId)
            Accessible.onPressAction: content.choose(index, true)
            TapHandler { onTapped: content.choose(finding.index, true) }
            ColumnLayout {
                id: findingLabels
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.margins: content.theme.space8
                spacing: content.theme.space4
                Label {
                    Layout.fillWidth: true
                    text: (finding.modelData.severity === "error" ? "Blocking" : "Check") + " · " +
                          String(finding.modelData.category || "structure").replace(/-/g, " ") + " · " +
                          (Number(finding.modelData.boardNumber || 0) > 0 ? "Board " + finding.modelData.boardNumber : "Document")
                    color: finding.modelData.severity === "error" ? content.theme.error : content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Label {
                    objectName: "carouselDocumentStructureMessage_" + finding.index
                    Layout.fillWidth: true
                    text: String(finding.modelData.message || "Structural finding")
                    color: content.theme.textMain
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeControl
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    elide: Text.ElideNone
                }
            }
        }
        Label {
            anchors.centerIn: parent
            width: parent.width - content.theme.space16
            visible: findings.count === 0
            text: content.root.documentStructureCategory === "all" ? "No structural findings." : "No findings in this category."
            color: content.theme.textMuted
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeCaption
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
    }
    Flow {
        objectName: "carouselDocumentStructureFooter"
        Layout.fillWidth: true
        Layout.preferredHeight: childrenRect.height
        spacing: content.theme.space8
        ActionButton {
            objectName: "carouselDocumentStructureRefreshButton"
            text: "Refresh"
            iconName: "refresh"
            compact: true
            onClicked: content.root.refreshDocumentStructureIssues()
        }
        ActionButton {
            objectName: "carouselDocumentStructureJumpButton"
            text: String(content.selectedIssue.itemId || "").length ? "Show layer" : "Show board"
            tone: "primary"
            compact: true
            enabled: Boolean(content.selectedIssue.canJump)
            disabledReason: content.selectedIssueId.length ? "This finding has no board or layer target." : "Select a finding to inspect."
            onClicked: {
                if (content.carouselDocument.focusDocumentStructureIssue(content.selectedIssueId)) {
                    content.carouselDocumentStructureDialog.focusCanvasOnClose = true;
                    content.carouselDocumentStructureDialog.close();
                }
            }
        }
    }
}
