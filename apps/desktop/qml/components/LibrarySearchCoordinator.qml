import QtQuick

QtObject {
    id: root
    property var catalog: null
    property var similarity: null
    property var setup: null
    property bool active: true
    property string text: catalog ? String(catalog.searchFilter || "") : ""
    property string contentQuery: ""
    property string message: ""
    readonly property var search: similarity ? similarity.controller : null
    readonly property bool searching: contentQuery.length > 0 && search && search.running
    readonly property bool pending: debounce.running || fallback.running
    readonly property string status: contentQuery.length > 0 && search ? search.status : message

    function cancelContent() {
        fallback.stop()
        if (contentQuery.length > 0 && similarity) similarity.cancel()
        contentQuery = ""
        message = ""
    }
    function edit(value) {
        cancelContent()
        text = String(value || "")
        debounce.restart()
    }
    function apply() {
        debounce.stop()
        if (!catalog) return
        if (contentQuery.length > 0) {
            if (search && search.running) return
            cancelContent()
        }
        catalog.searchFilter = text
        if (text.trim().length > 0 && catalog.assetCount === 0) fallback.restart()
    }
    function findContent() {
        if (!active || !catalog || catalog.loading || text.trim().length === 0 || contentQuery.length > 0) return
        if (catalog.assetCount > 0) return
        if (!search || !search.configured) {
            message = setup && setup.running ? "Checking photo search model…"
                                            : "Photo search needs a model. Open Settings → Models."
            return
        }
        // A canceled encoder request must drain before admitting its replacement.
        if (search.running) { fallback.restart(); return }
        contentQuery = text.trim()
        // Descriptions are not filenames. Keep project/rating/date filters, but
        // remove the text filter before capturing the content-search scope.
        catalog.searchFilter = ""
        similarity.search(contentQuery)
    }
    function clear() {
        debounce.stop()
        cancelContent()
        text = ""
        if (catalog) catalog.searchFilter = ""
    }
    onActiveChanged: if (!active) { debounce.stop(); cancelContent() }
    property Timer debounce: Timer { interval: 180; onTriggered: root.apply() }
    property Timer fallback: Timer { interval: 650; onTriggered: root.findContent() }
    property Connections catalogChanges: Connections {
        target: root.catalog
        function onFiltersChanged() {
            if (!root.debounce.running && root.contentQuery.length === 0)
                root.text = String(root.catalog.searchFilter || "")
        }
        function onLoadingChanged() {
            if (!root.catalog.loading && root.text.trim().length > 0 && root.contentQuery.length === 0)
                root.apply()
        }
    }
    property Connections setupChanges: Connections {
        target: root.setup
        function onChanged() {
            if (root.setup.ready && root.text.trim().length > 0 && root.contentQuery.length === 0)
                root.fallback.restart()
        }
    }
}
