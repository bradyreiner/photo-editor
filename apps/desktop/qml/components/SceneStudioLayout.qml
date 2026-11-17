pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Item {
    id: studio
    required property var viewport
    required property var cameraPanel
    required property var compositionPanel
    required property var workspaceSettings
    property bool active: false
    property alias viewportHost: stage
    property alias inspectorHost: inspectorContents
    property alias shotsHost: shotsContents
    readonly property bool showOutliner: active && workspaceSettings.outlinerVisible && width >= 980
    readonly property bool showInspector: active && workspaceSettings.inspectorVisible
    readonly property bool showShots: active && workspaceSettings.shotsVisible
    readonly property real headerHeight: active ? theme.scaled(64) : 0
    readonly property real deckHeight: showShots ? Math.min(theme.scaled(204), height * 0.32) : 0
    readonly property real sidebarWidth: width < 1200 ? theme.scaled(172) : theme.scaled(212)
    Theme { id: theme }
    function inspectCamera(page: int): void { cameraPanel.page = page; cameraPanel.open() }
    function inspectScene(section: int, id: string): void {
        compositionPanel.section = section
        compositionPanel.selectedId = id
        compositionPanel.open()
    }
    Item {
        id: stage
        objectName: "sceneStudioStage"
        anchors.fill: parent
        anchors.topMargin: studio.headerHeight
        anchors.leftMargin: studio.showOutliner ? outliner.width + theme.space8 : 0
        anchors.rightMargin: studio.showInspector ? inspector.width + theme.space8 : 0
        anchors.bottomMargin: studio.showShots ? studio.deckHeight + theme.space8 : 0
    }
    StudioPanel {
        id: outliner
        objectName: "sceneStudioOutliner"
        title: "Contents"
        visible: studio.showOutliner
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.topMargin: studio.headerHeight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: studio.showShots ? studio.deckHeight + theme.space8 : 0
        width: studio.sidebarWidth
        ScrollView {
            anchors.fill: parent
            contentWidth: availableWidth
            clip: true
            ColumnLayout {
                width: parent.width
                spacing: theme.space8
                ActionButton {
                    objectName: "sceneStudioCameraEntry"
                    Layout.fillWidth: true
                    text: "Camera"
                    onClicked: studio.inspectCamera(4)
                }
                Label {
                    text: "Scene objects"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    Layout.topMargin: theme.space8
                }
                Repeater {
                    model: (studio.viewport.status.sceneComposition || {}).objects || []
                    delegate: ActionButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: String(modelData.name || modelData.kind || "Object")
                        checkable: true
                        checked: studio.compositionPanel.selectedId === modelData.id && studio.compositionPanel.section === 0
                        tooltipText: "Select " + text
                        onClicked: studio.inspectScene(0, String(modelData.id))
                    }
                }
                ActionButton {
                    Layout.fillWidth: true
                    text: "Add or import…"
                    onClicked: studio.inspectScene(0, "")
                }
                Label {
                    text: "Lights"
                    color: theme.textMuted
                    font.family: theme.fontUi
                    font.pixelSize: theme.typeCaption
                    Layout.topMargin: theme.space8
                }
                Repeater {
                    model: (studio.viewport.status.sceneComposition || {}).lights || []
                    delegate: ActionButton {
                        required property var modelData
                        Layout.fillWidth: true
                        text: String(modelData.name || "Light")
                        checkable: true
                        checked: studio.compositionPanel.selectedId === modelData.id && studio.compositionPanel.section === 1
                        onClicked: studio.inspectScene(1, String(modelData.id))
                    }
                }
                ActionButton { Layout.fillWidth: true; text: "Add light…"; onClicked: studio.inspectScene(1, "") }
            }
        }
    }
    Rectangle {
        id: inspector
        objectName: "sceneStudioInspector"
        visible: studio.showInspector
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: studio.headerHeight
        anchors.bottom: parent.bottom
        anchors.bottomMargin: studio.showShots ? studio.deckHeight + theme.space8 : 0
        width: Math.min(Math.max(theme.scaled(280), Number(studio.workspaceSettings.inspectorWidth)), Math.min(theme.scaled(480), studio.width * 0.48))
        color: theme.ink900
        radius: theme.radiusPanel
        clip: true
        GridLayout {
            id: inspectorTabs
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.margins: theme.space8
            columns: 4
            columnSpacing: theme.space4
            rowSpacing: theme.space4
            ActionButton {
                objectName: "sceneStudioCameraTab"
                Layout.fillWidth: true
                compact: true; showText: false; iconName: "camera-lens"; checkable: true; autoExclusive: true
                active: checked
                accent: theme.textStrong
                accessibleName: "Camera"
                tooltipText: "Camera controls"
                checked: inspectorContents.activePanel === "camera" &&
                         [1, 2, 4].indexOf(Number(studio.cameraPanel.page)) >= 0
                onClicked: studio.inspectCamera(4)
            }
            ActionButton {
                objectName: "sceneStudioObjectsTab"
                Layout.fillWidth: true
                compact: true; showText: false; iconName: "geometry"; checkable: true; autoExclusive: true
                active: checked
                accent: theme.textStrong
                accessibleName: "Objects"
                tooltipText: "Scene objects"
                checked: inspectorContents.activePanel === "composition" &&
                         [0, 4].indexOf(Number(studio.compositionPanel.section)) >= 0
                onClicked: studio.inspectScene(0, String(studio.compositionPanel.selectedId || ""))
            }
            ActionButton {
                objectName: "sceneStudioLightingTab"
                Layout.fillWidth: true
                compact: true; showText: false; iconName: "exposure"; checkable: true; autoExclusive: true
                active: checked
                accent: theme.textStrong
                accessibleName: "Lighting"
                tooltipText: "Scene lighting"
                checked: inspectorContents.activePanel === "composition" &&
                         [1, 2, 3].indexOf(Number(studio.compositionPanel.section)) >= 0
                onClicked: studio.inspectScene(1, String(studio.compositionPanel.selectedId || ""))
            }
            ActionButton {
                objectName: "sceneStudioRenderTab"
                Layout.fillWidth: true
                compact: true; showText: false; iconName: "render"; checkable: true; autoExclusive: true
                active: checked
                accent: theme.textStrong
                accessibleName: "Render"
                tooltipText: "Color, render and effects"
                checked: inspectorContents.activePanel === "camera" &&
                         [0, 3, 5].indexOf(Number(studio.cameraPanel.page)) >= 0
                onClicked: studio.inspectCamera(0)
            }
        }
        Item {
            id: inspectorContents
            objectName: "sceneStudioInspectorContents"
            anchors.top: inspectorTabs.bottom
            anchors.bottom: parent.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            readonly property string activePanel: String(studio.workspaceSettings.inspectorPanel)
            enabled: Boolean(studio.viewport.status.frameAvailable)
            readonly property bool inspectorVisible: studio.showInspector
            function showPanel(key: string): void {
                studio.workspaceSettings.inspectorPanel = key
                studio.workspaceSettings.inspectorVisible = true
            }
            function closePanel(): void { studio.workspaceSettings.inspectorVisible = false }
        }
        MouseArea {
            objectName: "sceneStudioInspectorResizeHandle"
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: theme.space8
            cursorShape: Qt.SplitHCursor
            property real pressX: 0
            property real pressWidth: 0
            onPressed: mouse => {
                pressX = mapToItem(studio, mouse.x, mouse.y).x
                pressWidth = inspector.width
            }
            onPositionChanged: mouse => {
                if (pressed) studio.workspaceSettings.inspectorWidth = Math.max(theme.scaled(280),
                    Math.min(theme.scaled(480), pressWidth - (mapToItem(studio, mouse.x, mouse.y).x - pressX)))
            }
        }
    }
    StudioPanel {
        objectName: "sceneStudioShots"
        visible: studio.showShots
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: studio.deckHeight
        title: "Shots & camera path"
        subhead: Number(studio.viewport.status.pathDuration || 0).toFixed(1) + " s"
        Item { id: shotsContents; anchors.fill: parent }
    }
}
