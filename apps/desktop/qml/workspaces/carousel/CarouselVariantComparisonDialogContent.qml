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
    required property var carouselVariantComparisonDialog
    readonly property var comparison: content.carouselVariantComparisonDialog.comparison
    readonly property bool activeComparison: Boolean(comparison.active)
    readonly property bool currentPreview: activeComparison && String(comparison.rightHash) === content.carouselDocument.documentContentHash
    readonly property int boardIndex: content.carouselVariantComparisonDialog.boardIndex
    readonly property bool leftPresent: boardIndex < Number(comparison.leftBoardCount || 0)
    readonly property bool rightPresent: boardIndex < Number(comparison.rightBoardCount || 0)
    readonly property bool previewsReady: activeComparison && (!leftPresent || leftImage.status === Image.Ready) &&
                                         (!rightPresent || rightImage.status === Image.Ready)
    readonly property bool previewError: (leftPresent && leftImage.status === Image.Error) || (rightPresent && rightImage.status === Image.Error)
    readonly property var decisions: [
        { key: "keep-right", label: "Keep current", explanation: "Keep the current document and discard this capture." },
        { key: "keep-left", label: "Restore captured", explanation: "Restore the complete captured document. One Undo returns to the current design." },
        { key: "keep-both", label: "Save both as versions", explanation: "Keep the current document and save both full versions. One Undo removes the pair." }
    ]
    readonly property string selectedDecision: decisions[Math.max(0, decision.currentIndex)].key
    property string actionError: ""
    property int previewRevision: 0
    property real previewZoom: 0
    property bool synchronizingPan: false
    function setPreviewZoom(value) {
        previewZoom = value
        leftViewport.contentX = 0; leftViewport.contentY = 0
        rightViewport.contentX = 0; rightViewport.contentY = 0
    }
    function synchronizePan(source, target) {
        if (synchronizingPan || previewZoom === 0) return;
        synchronizingPan = true
        const sourceX = Math.max(0, source.contentWidth - source.width)
        const sourceY = Math.max(0, source.contentHeight - source.height)
        target.contentX = sourceX > 0 ? source.contentX / sourceX * Math.max(0, target.contentWidth - target.width) : 0
        target.contentY = sourceY > 0 ? source.contentY / sourceY * Math.max(0, target.contentHeight - target.height) : 0
        synchronizingPan = false
    }
    function panVertical(direction, fine) {
        if (previewZoom === 0) return;
        const viewport = leftPresent ? leftViewport : rightViewport
        viewport.contentY = Math.max(0, Math.min(Math.max(0, viewport.contentHeight - viewport.height), viewport.contentY + direction * (fine ? content.theme.scaled(24) : viewport.height * 0.8)))
    }
    anchors.fill: parent
    anchors.margins: content.theme.scaled(18)
    spacing: content.theme.space12

    function focusInitialTarget() {
        actionError = ""
        decision.currentIndex = 0
        nameFlickable.contentY = 0
        setPreviewZoom(0)
        if (activeComparison) previewArea.forceActiveFocus()
        else closeButton.forceActiveFocus()
    }
    function navigate(index) {
        content.carouselDocument.setVariantComparisonBoardIndex(index)
    }
    function resolveDecision() {
        if (!currentPreview || !previewsReady) return;
        if (content.carouselDocument.resolveVariantComparison(content.selectedDecision))
            content.carouselVariantComparisonDialog.close()
        else actionError = content.carouselDocument.status
    }
    Connections {
        target: content.carouselDocument
        function onVariantComparisonChanged() { content.previewRevision += 1 }
    }

    RowLayout {
        objectName: "carouselVariantComparisonHeader"
        Layout.fillWidth: true
        spacing: content.theme.space8
        Label {
            Layout.fillWidth: true
            text: "Compare design variants"
            color: content.theme.textStrong
            font.family: content.theme.fontUi
            font.pixelSize: content.theme.typeDialogTitle
            font.weight: Font.DemiBold
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        ActionButton {
            id: closeButton
            objectName: "carouselVariantComparisonCloseButton"
            compact: true
            iconName: "close"
            text: "Close"
            onClicked: content.carouselVariantComparisonDialog.close()
        }
    }
    ScrollView {
        id: nameArea
        objectName: "carouselVariantComparisonNameScroll"
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(content.theme.scaled(60), capturedName.implicitHeight + content.theme.space8)
        padding: content.theme.space4
        contentWidth: availableWidth
        contentHeight: capturedName.implicitHeight
        clip: true
        activeFocusOnTab: contentHeight > availableHeight
        Accessible.name: "Captured variant name"
        ScrollBar.vertical.policy: contentHeight > availableHeight ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        background: Rectangle {
            color: "transparent"
            radius: content.theme.radiusControl
            border.width: nameArea.activeFocus ? content.theme.focusBorderWidth : 0
            border.color: content.theme.focusRing
        }
        function scrollName(amount) {
            nameFlickable.contentY = Math.max(0, Math.min(nameFlickable.contentHeight - nameFlickable.height, nameFlickable.contentY + amount))
        }
        Keys.onUpPressed: scrollName(-content.theme.scaled(24))
        Keys.onDownPressed: scrollName(content.theme.scaled(24))
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home) nameFlickable.contentY = 0
            else if (event.key === Qt.Key_End) nameFlickable.contentY = Math.max(0, nameFlickable.contentHeight - nameFlickable.height)
            else { event.accepted = false; return; }
            event.accepted = true
        }
        contentItem: Flickable {
            id: nameFlickable
            contentWidth: width
            contentHeight: capturedName.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            Label {
                id: capturedName
                objectName: "carouselVariantComparisonCapturedName"
                width: nameFlickable.width
                text: content.activeComparison ? String(content.comparison.leftName || "Captured variant") : "Capture a variant in Design variants to compare."
                color: content.theme.textMain
                font.family: content.theme.fontUi
                font.pixelSize: content.theme.typeControl
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                elide: Text.ElideNone
            }
        }
    }
    RowLayout {
        objectName: "carouselVariantComparisonNavigation"
        Layout.fillWidth: true
        visible: content.activeComparison
        spacing: content.theme.space8
        ActionButton {
            objectName: "carouselVariantComparisonPreviousButton"
            compact: true
            showText: false
            iconName: "chevron-left"
            tooltipText: "Previous board"
            enabled: content.boardIndex > 0
            disabledReason: "Already on the first board."
            onClicked: content.navigate(content.boardIndex - 1)
        }
        Label {
            objectName: "carouselVariantComparisonBoardLabel"
            Layout.fillWidth: true
            text: "Board " + (content.boardIndex + 1) + " of " + content.carouselVariantComparisonDialog.boardCount + " · " +
                  (content.comparison.different ? "Documents differ" : "Documents match")
            color: content.theme.textMuted
            font.family: content.theme.fontMono
            font.pixelSize: content.theme.typeCaption
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        MenuButton {
            objectName: "carouselVariantComparisonZoomMenu"
            compact: true
            text: content.previewZoom === 0 ? "Fit" : Math.round(content.previewZoom * 100) + "%"
            iconName: "search"
            primaryOpensMenu: true
            accessibleName: "Comparison zoom"
            tooltipText: "Zoom relative to the rendered preview image. Up and Down pan both boards."
            menuItems: [{ key: "fit", label: "Fit boards" }, { key: "100", label: "100% preview" }, { key: "200", label: "200% preview" }]
            onMenuItemTriggered: function(key) { content.setPreviewZoom(key === "fit" ? 0 : Number(key) / 100) }
        }
        ActionButton {
            objectName: "carouselVariantComparisonNextButton"
            compact: true
            showText: false
            iconName: "chevron-right"
            tooltipText: "Next board"
            enabled: content.boardIndex + 1 < content.carouselVariantComparisonDialog.boardCount
            disabledReason: "Already on the last board."
            onClicked: content.navigate(content.boardIndex + 1)
        }
    }
    Item {
        id: previewArea
        objectName: "carouselVariantComparisonPreviews"
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: content.theme.scaled(80)
        activeFocusOnTab: content.activeComparison
        Accessible.role: Accessible.Pane
        Accessible.name: "Captured and current board previews"
        Accessible.description: "Use Left and Right to change boards, or Home and End to reach the first and last board. Up and Down pan both zoomed previews. Page Up and Page Down move farther."
        Keys.onUpPressed: event => {
            if (content.previewZoom > 0) content.panVertical(-1, true)
            else event.accepted = false
        }
        Keys.onDownPressed: event => {
            if (content.previewZoom > 0) content.panVertical(1, true)
            else event.accepted = false
        }
        Keys.onLeftPressed: content.navigate(content.boardIndex - 1)
        Keys.onRightPressed: content.navigate(content.boardIndex + 1)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Home) content.navigate(0)
            else if (event.key === Qt.Key_End) content.navigate(content.carouselVariantComparisonDialog.boardCount - 1)
            else if (event.key === Qt.Key_PageDown && content.previewZoom > 0) content.panVertical(1, false)
            else if (event.key === Qt.Key_PageUp && content.previewZoom > 0) content.panVertical(-1, false)
            else { event.accepted = false; return; }
            event.accepted = true
        }
        Rectangle {
            anchors.fill: parent
            color: "transparent"
            radius: content.theme.radiusControl
            border.width: previewArea.activeFocus ? content.theme.focusBorderWidth : 0
            border.color: content.theme.focusRing
        }
        RowLayout {
            anchors.fill: parent
            anchors.margins: content.theme.space4
            spacing: content.theme.space12
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                spacing: content.theme.space6
                Label {
                    objectName: "carouselVariantComparisonLeftHeading"
                    Layout.fillWidth: true
                    text: "Captured · " + Number(content.comparison.leftBoardCount || 0) + " boards"
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: content.theme.ink950
                    radius: content.theme.radiusControl
                    Flickable {
                        id: leftViewport
                        objectName: "carouselVariantComparisonLeftViewport"
                        anchors.fill: parent
                        clip: true
                        contentWidth: Math.max(width, leftImage.width + content.theme.space16)
                        contentHeight: Math.max(height, leftImage.height + content.theme.space16)
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: content.previewZoom > 0
                        onContentXChanged: content.synchronizePan(leftViewport, rightViewport)
                        onContentYChanged: content.synchronizePan(leftViewport, rightViewport)
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; visible: leftViewport.contentHeight > leftViewport.height }
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; visible: leftViewport.contentWidth > leftViewport.width }
                        Image {
                            id: leftImage
                            objectName: "carouselVariantComparisonLeftImage"
                            width: content.previewZoom > 0 ? Math.max(1, implicitWidth) * content.previewZoom : Math.max(1, leftViewport.width - content.theme.space16)
                            height: content.previewZoom > 0 ? Math.max(1, implicitHeight) * content.previewZoom : Math.max(1, leftViewport.height - content.theme.space16)
                            x: (leftViewport.contentWidth - width) / 2
                            y: (leftViewport.contentHeight - height) / 2
                            cache: false
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            readonly property string renderSource: content.leftPresent ? String(content.carouselVariantComparisonDialog.leftSources[content.boardIndex] || "") : ""
                            readonly property int revision: content.previewRevision
                            function reload() {
                                source = ""
                                Qt.callLater(function() { leftImage.source = leftImage.renderSource })
                            }
                            onRenderSourceChanged: reload()
                            onRevisionChanged: reload()
                            Component.onCompleted: reload()
                        }
                    }
                    Label {
                        objectName: "carouselVariantComparisonLeftState"
                        anchors.centerIn: parent
                        width: parent.width - content.theme.space16
                        visible: leftImage.status !== Image.Ready
                        text: !content.activeComparison ? "No capture" : !content.leftPresent ? "This board is absent from the capture." :
                              leftImage.status === Image.Error ? "Captured preview unavailable." : "Loading preview…"
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeControl
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.preferredWidth: 1
                Layout.fillHeight: true
                spacing: content.theme.space6
                Label {
                    objectName: "carouselVariantComparisonRightHeading"
                    Layout.fillWidth: true
                    text: "Current · " + Number(content.comparison.rightBoardCount || 0) + " boards"
                    color: content.theme.textMuted
                    font.family: content.theme.fontUi
                    font.pixelSize: content.theme.typeCaption
                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: content.theme.ink950
                    radius: content.theme.radiusControl
                    Flickable {
                        id: rightViewport
                        objectName: "carouselVariantComparisonRightViewport"
                        anchors.fill: parent
                        clip: true
                        contentWidth: Math.max(width, rightImage.width + content.theme.space16)
                        contentHeight: Math.max(height, rightImage.height + content.theme.space16)
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: content.previewZoom > 0
                        onContentXChanged: content.synchronizePan(rightViewport, leftViewport)
                        onContentYChanged: content.synchronizePan(rightViewport, leftViewport)
                        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded; visible: rightViewport.contentHeight > rightViewport.height }
                        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded; visible: rightViewport.contentWidth > rightViewport.width }
                        Image {
                            id: rightImage
                            objectName: "carouselVariantComparisonRightImage"
                            width: content.previewZoom > 0 ? Math.max(1, implicitWidth) * content.previewZoom : Math.max(1, rightViewport.width - content.theme.space16)
                            height: content.previewZoom > 0 ? Math.max(1, implicitHeight) * content.previewZoom : Math.max(1, rightViewport.height - content.theme.space16)
                            x: (rightViewport.contentWidth - width) / 2
                            y: (rightViewport.contentHeight - height) / 2
                            cache: false
                            asynchronous: true
                            fillMode: Image.PreserveAspectFit
                            readonly property string renderSource: content.rightPresent ? String(content.carouselVariantComparisonDialog.rightSources[content.boardIndex] || "") : ""
                            readonly property int revision: content.previewRevision
                            function reload() {
                                source = ""
                                Qt.callLater(function() { rightImage.source = rightImage.renderSource })
                            }
                            onRenderSourceChanged: reload()
                            onRevisionChanged: reload()
                            Component.onCompleted: reload()
                        }
                    }
                    Label {
                        objectName: "carouselVariantComparisonRightState"
                        anchors.centerIn: parent
                        width: parent.width - content.theme.space16
                        visible: rightImage.status !== Image.Ready
                        text: !content.activeComparison ? "No comparison" : !content.rightPresent ? "This board is absent from the current document." :
                              rightImage.status === Image.Error ? "Current preview unavailable." : "Loading preview…"
                        color: content.theme.textMuted
                        font.family: content.theme.fontUi
                        font.pixelSize: content.theme.typeControl
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }
    }
    Label {
        objectName: "carouselVariantComparisonGuidance"
        Layout.fillWidth: true
        text: content.actionError.length ? content.actionError : !content.activeComparison ? "Close this review and capture the document first." :
              !content.currentPreview ? "The document changed after this preview. Refresh before choosing a version." :
              content.previewError ? "A board preview is unavailable. Refresh to render it again." :
              !content.previewsReady ? "Wait for both available board previews to load." : content.decisions[Math.max(0, decision.currentIndex)].explanation
        color: content.actionError.length || (content.activeComparison && !content.currentPreview) ? content.theme.warning : content.theme.textMuted
        font.family: content.theme.fontUi
        font.pixelSize: content.theme.typeCaption
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    }
    RowLayout {
        objectName: "carouselVariantComparisonFooter"
        Layout.fillWidth: true
        visible: content.activeComparison
        spacing: content.theme.space8
        ToneComboBox {
            id: decision
            objectName: "carouselVariantComparisonDecision"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            model: content.decisions
            textRole: "label"
            accessibleLabel: "Design variant decision"
        }
        ActionButton {
            objectName: "carouselVariantComparisonRefreshButton"
            compact: true
            visible: !content.currentPreview || content.previewError
            text: "Refresh"
            onClicked: {
                if (content.carouselDocument.refreshVariantComparison()) content.actionError = ""
                else content.actionError = content.carouselDocument.status
            }
        }
        ActionButton {
            objectName: "carouselVariantComparisonApplyButton"
            tone: "primary"
            text: "Apply decision"
            enabled: content.currentPreview && content.previewsReady
            disabledReason: !content.currentPreview ? "Refresh the current document preview." : "Wait for the board previews to load."
            onClicked: content.resolveDecision()
        }
    }
}
