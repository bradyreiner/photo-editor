import QtQml

QtObject {
    id: root

    required property string query
    required property var maskSets
    required property var toolItems
    required property var adjustmentItems

    function searchText() {
        return root.query.trim().toLowerCase()
    }

    function matches(text) {
        const normalizedQuery = root.searchText()
        return normalizedQuery.length === 0 || String(text || "").toLowerCase().indexOf(normalizedQuery) >= 0
    }

    function sourceMatches(source) {
        const searchable = ((source.name || "") + " " +
                            (source.kind || "") + " " +
                            (source.operation || "") + " " +
                            (source.maskSetName || "") + " " +
                            (source.sourceBadge || "") + " " +
                            (source.operationBadge || "") + " " +
                            (source.sourceAuditLabel || ""))
        return root.matches(searchable)
    }

    function setMatches(maskSet) {
        if (root.searchText().length === 0)
            return true
        if (root.matches((maskSet.name || "") + " " + (maskSet.id || "")))
            return true
        const sources = maskSet.sources || []
        for (let index = 0; index < sources.length; ++index) {
            if (root.sourceMatches(sources[index]))
                return true
        }
        return false
    }

    function results() {
        if (root.searchText().length === 0)
            return []
        const matches = []
        const addIfMatch = function(item) {
            if (root.matches(item.label + " " + item.detail + " " + item.keywords))
                matches.push(item)
        }
        root.toolItems.forEach(addIfMatch)
        for (let setIndex = 0; setIndex < root.maskSets.length; ++setIndex) {
            const maskSet = root.maskSets[setIndex]
            const sources = maskSet.sources || []
            if (root.matches((maskSet.name || "") + " mask components")) {
                matches.push({ category: "Mask", label: maskSet.name, detail: sources.length + " components", flatIndex: sources.length > 0 ? sources[0].flatIndex : -1 })
            }
            for (let sourceIndex = 0; sourceIndex < sources.length; ++sourceIndex) {
                const source = sources[sourceIndex]
                if (root.sourceMatches(source)) {
                    matches.push({ category: "Mask", label: source.name || source.kind, detail: (source.maskSetName || maskSet.name || "Mask") + " · " + (source.operationBadge || source.operation || "add"), flatIndex: source.flatIndex })
                }
            }
        }
        root.adjustmentItems.forEach(addIfMatch)
        return matches.slice(0, 12)
    }
}
