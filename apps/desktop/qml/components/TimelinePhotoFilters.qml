pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

ColumnLayout {
    id: root
    objectName: "timelinePhotoFilters"
    required property var theme
    required property var documentController
    required property var currentClip
    property var filters: []
    property var sequenceTrackRowsSnapshot: []
    property string searchText: ""
    property string expandedFilterId: ""
    property string message: ""
    property bool editing: false
    property bool publishing: false
    property string gestureClipId: ""
    readonly property string clipId: String(currentClip && currentClip.clipId || "")
    readonly property bool supported: currentClip && ["video", "adjustment"].indexOf(String(currentClip.mediaType)) >= 0
    readonly property bool trackLocked: {
        if (!documentController || !root.visible) return true
        for (const track of sequenceTrackRowsSnapshot) {
            if (String(track.trackId) === String(currentClip && currentClip.trackId)) return track.locked === true
        }
        return true
    }
    readonly property bool canEdit: supported && !trackLocked
    readonly property var catalog: documentController && typeof documentController.availableClipPhotoFilters === "function"
                                   ? documentController.availableClipPhotoFilters() : []
    spacing: theme.space6

    function refreshSequenceTrackRowsSnapshot() {
        if (!root.visible) return
        if (!documentController || typeof documentController.snapshotSequenceTracks !== "function") {
            sequenceTrackRowsSnapshot = []
            return
        }
        sequenceTrackRowsSnapshot = documentController.snapshotSequenceTracks()
    }

    function refresh() {
        if (!root.visible) return
        if (!documentController) { filters = []; return }
        if (!editing && typeof documentController.clipPhotoFilters === "function") {
            const next = documentController.clipPhotoFilters(clipId)
            if (JSON.stringify(next) !== JSON.stringify(filters)) filters = next
        }
    }
    function edit(operation, id, type, values) {
        if (!canEdit || typeof documentController.editClipPhotoFilter !== "function") return false
        const target = editing ? gestureClipId : clipId
        if (target !== clipId) return false
        publishing = true
        const result = documentController.editClipPhotoFilter(target, operation, id, type, values)
        publishing = false
        message = result && result.ok === true ? "" : String(result && result.error || "Filter could not be updated.")
        if (!editing) Qt.callLater(root.refresh)
        return result && result.ok === true
    }
    function beginEdit() {
        if (editing || !canEdit) return
        gestureClipId = clipId
        editing = true
        documentController.beginContinuousEdit()
    }
    function endEdit() {
        if (!editing) return
        if (documentController) documentController.endContinuousEdit()
        editing = false
        gestureClipId = ""
        Qt.callLater(root.refresh)
    }
    onClipIdChanged: { endEdit(); if (visible) refresh() }
    onCurrentClipChanged: if (visible && !editing && !publishing) Qt.callLater(root.refresh)
    onVisibleChanged: {
        if (visible) {
            refreshSequenceTrackRowsSnapshot()
            refresh()
        }
        else endEdit()
    }
    onDocumentControllerChanged: if (visible) refreshSequenceTrackRowsSnapshot()
    Component.onCompleted: {
        refreshSequenceTrackRowsSnapshot()
        refresh()
    }
    Component.onDestruction: endEdit()

    Connections {
        target: root.documentController
        function onSequenceChanged() { root.refreshSequenceTrackRowsSnapshot() }
    }

    RowLayout {
        Layout.fillWidth: true
        Label {
            Layout.fillWidth: true
            text: "Filters from Edit"
            color: root.theme.textStrong
            font.family: root.theme.fontUi
            font.pixelSize: root.theme.typeControl
        }
        MenuButton {
            objectName: "timelineAddPhotoFilterButton"
            text: "Add filter"
            iconName: "plus"
            compact: true
            tone: "quiet"
            primaryOpensMenu: true
            menuWidth: 320
            menuItems: root.catalog.filter(function(entry) {
                const query = root.searchText.trim().toLowerCase()
                return !query || (entry.name + " " + entry.category + " " + entry.description).toLowerCase().indexOf(query) >= 0
            }).map(function(entry) {
                return {key: entry.type, label: entry.name, description: entry.category,
                    enabled: root.canEdit && entry.enabled !== false,
                    disabledReason: !root.supported ? "Select one video or adjustment clip."
                        : root.trackLocked ? "Unlock the selected track." : entry.disabledReason}
            })
            onMenuItemTriggered: function(key) {
                if (root.edit("add", "", key, {})) {
                    Qt.callLater(function() {
                        root.refresh()
                        const rows = root.filters
                        root.expandedFilterId = rows.length ? rows[rows.length - 1].id : ""
                    })
                }
            }
        }
    }
    ToneTextField {
        objectName: "timelinePhotoFilterSearch"
        Layout.fillWidth: true
        placeholderText: "Search Edit filters"
        accessibleLabel: "Search filters shared with Edit"
        text: root.searchText
        onTextEdited: root.searchText = text
    }
    Label {
        Layout.fillWidth: true
        visible: !root.supported || root.message.length > 0
        text: root.message || "Select one video or adjustment clip to apply a filter."
        color: root.message ? root.theme.warning : root.theme.textMuted
        font.family: root.theme.fontUi
        font.pixelSize: root.theme.typeCaption
        wrapMode: Text.Wrap
    }
    Label {
        objectName: "timelinePhotoFilterExportStatus"
        Layout.fillWidth: true
        readonly property bool pending: root.currentClip && root.currentClip.colorGradeSequenceExportPending === true
        readonly property bool usesSourceBake: root.currentClip && root.currentClip.colorGradeSequenceExportUsesSourceBake === true
        visible: root.filters.length > 0 && root.currentClip
                 && (pending || usesSourceBake || root.currentClip.colorGradeSequenceExportSupported === false)
        text: pending ? "Checking export compatibility…"
              : usesSourceBake ? "Export uses a source-space bake before crop and placement. Delivery requires normal source timing, constant frame rate from 1–120 fps, no more than 180,000 source frames, and available bounded temporary storage."
              : "Export: " + String(root.currentClip && root.currentClip.colorGradeSequenceExportError
                                    || "This filter stack is unavailable with the current clip setup.")
        color: pending || usesSourceBake ? root.theme.textMuted : root.theme.warning
        font.family: root.theme.fontUi
        font.pixelSize: root.theme.typeCaption
        wrapMode: Text.Wrap
    }
    Repeater {
        model: root.filters
        delegate: ColumnLayout {
            id: filterRow
            required property var modelData
            required property int index
            property bool expanded: root.expandedFilterId === String(modelData.id)
            Layout.fillWidth: true
            spacing: root.theme.space4
            RowLayout {
                Layout.fillWidth: true
                ActionButton {
                    objectName: "timelinePhotoFilterExpand_" + filterRow.index
                    Layout.fillWidth: true
                    text: String(filterRow.modelData.name)
                    iconName: filterRow.expanded ? "chevron-down" : "chevron-right"
                    compact: true
                    tone: "quiet"
                    allowLabelWrap: true
                    onClicked: root.expandedFilterId = filterRow.expanded ? "" : String(filterRow.modelData.id)
                }
                ActionButton {
                    objectName: "timelinePhotoFilterEnabled_" + filterRow.index
                    text: "Enable filter"
                    iconName: "visible"
                    showText: false
                    compact: true
                    tone: "quiet"
                    active: filterRow.modelData.enabled === true
                    enabled: root.canEdit
                    onClicked: root.edit("enabled", filterRow.modelData.id, "", {enabled: !filterRow.modelData.enabled})
                }
                MenuButton {
                    text: "Filter actions"
                    iconName: "more"
                    showText: false
                    compact: true
                    tone: "quiet"
                    primaryOpensMenu: true
                    menuItems: [
                        {key: "up", label: "Move earlier", enabled: root.canEdit && Number(filterRow.modelData.position) > 0},
                        {key: "down", label: "Move later", enabled: root.canEdit && filterRow.index + 1 < root.filters.length},
                        {key: "remove", label: "Remove filter", enabled: root.canEdit, separatorBefore: true}
                    ]
                    onMenuItemTriggered: function(key) {
                        root.edit(key === "remove" ? "remove" : "move", filterRow.modelData.id, "",
                            key === "remove" ? {} : {position: Number(filterRow.modelData.position) + (key === "up" ? -1 : 1)})
                    }
                }
            }
            InspectorSlider {
                objectName: "timelinePhotoFilterAmount_" + filterRow.index
                visible: filterRow.expanded
                Layout.fillWidth: true
                label: "Amount"
                from: 0
                to: 100
                stepSize: 1
                value: Number(filterRow.modelData.amount) * 100
                valueText: Number(value).toFixed(0) + "%"
                enabled: root.canEdit
                onInteractionStarted: root.beginEdit()
                onValueDragged: function(value) { root.edit("amount", filterRow.modelData.id, "", {amount: value / 100}) }
                defaultValue: 100
                onResetRequested: root.edit("amount", filterRow.modelData.id, "", {amount: 1})
                onInteractionFinished: root.endEdit()
            }
            Repeater {
                model: filterRow.expanded ? filterRow.modelData.fields : []
                delegate: InspectorSlider {
                    required property var modelData
                    objectName: "timelinePhotoFilterParameter_" + String(modelData.key)
                    Layout.fillWidth: true
                    label: String(modelData.label)
                    from: Number(modelData.minimum)
                    to: Number(modelData.maximum)
                    stepSize: Number(modelData.step)
                    value: Number(modelData.value)
                    valueText: Number(value).toFixed(Number(modelData.step) >= 1 ? 0 : Number(modelData.step) < 0.01 ? 3 : 2)
                    enabled: root.canEdit
                    onInteractionStarted: root.beginEdit()
                    onValueDragged: function(value) { root.edit("parameter", filterRow.modelData.id, "", {parameter: modelData.key, value: value}) }
                    defaultValue: Number(modelData.defaultValue)
                    onResetRequested: root.edit("parameter", filterRow.modelData.id, "", {parameter: modelData.key, value: Number(modelData.defaultValue)})
                    onInteractionFinished: root.endEdit()
                }
            }
        }
    }
}
