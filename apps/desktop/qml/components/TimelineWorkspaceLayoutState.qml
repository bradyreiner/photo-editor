import QtQml

QtObject {
    id: root
    property var layouts: ({})
    readonly property var panelIds: ["media", "source", "effects", "inspector", "color", "mixer",
                                    "keyframes", "titles", "captions", "transcript", "scopes", "review"]
    readonly property var dockIds: ["left", "right", "auxiliary", "bottom", "floating"]

    function validPreset(key) {
        return ["edit", "color", "audio", "finish"].indexOf(String(key)) >= 0
    }

    function bounded(value, fallback, low, high) {
        const number = Number(value)
        return isFinite(number) ? Math.max(low, Math.min(high, number)) : fallback
    }

    function plainObject(value) {
        return value !== null && typeof value === "object" && !Array.isArray(value)
    }

    function panelPlacements(key) {
        if (key === "edit") {
            return { media: "left", source: "left", inspector: "left", effects: "right",
                     color: "right", scopes: "auxiliary", mixer: "left", keyframes: "right",
                     titles: "right", captions: "bottom", transcript: "bottom", review: "right" }
        }
        if (key === "color") {
            return { media: "left", source: "left", effects: "auxiliary", inspector: "right",
                     color: "right", scopes: "bottom", mixer: "bottom", keyframes: "bottom",
                     titles: "bottom", captions: "bottom", transcript: "bottom", review: "right" }
        }
        if (key === "audio") {
            return { media: "left", source: "left", effects: "right", inspector: "right",
                     color: "auxiliary", scopes: "right", mixer: "bottom", keyframes: "bottom",
                     titles: "bottom", captions: "bottom", transcript: "bottom", review: "right" }
        }
        return { media: "left", source: "left", effects: "right", inspector: "right",
                 color: "right", scopes: "auxiliary", mixer: "bottom", keyframes: "bottom",
                 titles: "bottom", captions: "bottom", transcript: "bottom", review: "right" }
    }

    function defaultDockPanels(key) {
        const visible = {
            media: key !== "color", source: false, effects: key === "edit",
            inspector: key === "edit" || key === "finish",
            color: key === "edit" || key === "color",
            mixer: key === "edit" || key === "audio", keyframes: false,
            titles: key === "edit", captions: key === "finish",
            transcript: false, scopes: key === "color", review: false
        }
        const placements = panelPlacements(key)
        const defaultOrders = {
            media: 0, source: 1, effects: 0, inspector: 1,
            color: 0, scopes: 1, mixer: key === "edit" ? 2 : 0, keyframes: 1,
            titles: 2, captions: 3, transcript: 4, review: 2
        }
        const result = ({})
        for (let index = 0; index < panelIds.length; ++index) {
            const panelId = panelIds[index]
            result[panelId] = {
                visible: visible[panelId],
                dock: placements[panelId],
                order: defaultOrders[panelId],
                returnDock: placements[panelId],
                floatingGeometry: defaultFloatingGeometry(panelId)
            }
        }
        // Factory arrangements must already be canonical. Saving a default
        // layout must not silently reorder tied or noncontiguous tab indices.
        for (let dockIndex = 0; dockIndex < dockIds.length; ++dockIndex) {
            const dockId = dockIds[dockIndex]
            const members = panelIds.filter(function(id) { return result[id].dock === dockId })
            members.sort(function(first, second) {
                return result[first].order - result[second].order
                        || panelIds.indexOf(first) - panelIds.indexOf(second)
            })
            for (let index = 0; index < members.length; ++index)
                result[members[index]].order = index
        }
        return result
    }

    function defaultFloatingGeometry(panelId) {
        const wide = ["media", "mixer", "transcript"].indexOf(panelId) >= 0
        return { x: 120, y: 90, width: wide ? 520 : 420, height: wide ? 420 : 360 }
    }

    function normalizeDockPanels(value, key) {
        const fallback = defaultDockPanels(key)
        const result = ({})
        for (let index = 0; index < panelIds.length; ++index) {
            const panelId = panelIds[index]
            const saved = plainObject(value) && plainObject(value[panelId]) ? value[panelId] : ({})
            const defaults = fallback[panelId]
            const savedDock = String(saved.dock || "")
            const savedReturnDock = String(saved.returnDock || "")
            const geometry = plainObject(saved.floatingGeometry) ? saved.floatingGeometry : ({})
            result[panelId] = {
                visible: typeof saved.visible === "boolean" ? saved.visible : defaults.visible,
                dock: dockIds.indexOf(savedDock) >= 0 ? savedDock : defaults.dock,
                order: Math.round(bounded(saved.order, defaults.order, 0, 64)),
                returnDock: dockIds.indexOf(savedReturnDock) >= 0 && savedReturnDock !== "floating"
                            ? savedReturnDock : defaults.returnDock,
                floatingGeometry: {
                    x: Math.round(bounded(geometry.x, defaults.floatingGeometry.x, -10000, 10000)),
                    y: Math.round(bounded(geometry.y, defaults.floatingGeometry.y, -10000, 10000)),
                    width: Math.round(bounded(geometry.width, defaults.floatingGeometry.width, 220, 1600)),
                    height: Math.round(bounded(geometry.height, defaults.floatingGeometry.height, 180, 1200))
                }
            }
        }
        for (let dockIndex = 0; dockIndex < dockIds.length; ++dockIndex) {
            const dockId = dockIds[dockIndex]
            const members = panelIds.filter(function(panelId) { return result[panelId].dock === dockId })
            members.sort(function(first, second) {
                const orderDifference = result[first].order - result[second].order
                return orderDifference !== 0 ? orderDifference : panelIds.indexOf(first) - panelIds.indexOf(second)
            })
            for (let memberIndex = 0; memberIndex < members.length; ++memberIndex)
                result[members[memberIndex]].order = memberIndex
        }
        return result
    }

    function defaultActivePanelByDock(key) {
        if (key === "color")
            return { left: "", right: "color", auxiliary: "", bottom: "scopes", floating: "" }
        if (key === "audio")
            return { left: "media", right: "", auxiliary: "", bottom: "mixer", floating: "" }
        if (key === "finish")
            return { left: "media", right: "inspector", auxiliary: "", bottom: "captions", floating: "" }
        return { left: "inspector", right: "effects", auxiliary: "", bottom: "", floating: "" }
    }

    function normalizeActivePanelByDock(value, panels, key) {
        const defaults = defaultActivePanelByDock(key)
        const result = ({})
        for (let dockIndex = 0; dockIndex < dockIds.length; ++dockIndex) {
            const dockId = dockIds[dockIndex]
            const requested = plainObject(value) ? String(value[dockId] || "") : ""
            const requestedIsValid = panelIds.indexOf(requested) >= 0 && panels[requested].visible &&
                                     panels[requested].dock === dockId
            const fallback = defaults[dockId]
            const fallbackIsValid = panelIds.indexOf(fallback) >= 0 && panels[fallback].visible &&
                                    panels[fallback].dock === dockId
            if (requestedIsValid) {
                result[dockId] = requested
            } else if (fallbackIsValid) {
                result[dockId] = fallback
            } else {
                const members = panelIds.filter(function(panelId) {
                    return panels[panelId].visible && panels[panelId].dock === dockId
                })
                members.sort(function(first, second) { return panels[first].order - panels[second].order })
                result[dockId] = members.length > 0 ? members[0] : ""
            }
        }
        return result
    }

    function defaults(key) {
        return {
            sourceCollapsed: key !== "edit", sourceHidden: false,
            inspectorCollapsed: false, sourceDetached: false,
            inspectorDetached: false, sourceWidth: key === "edit" ? 400 : 300,
            inspectorWidth: key === "audio" ? 400 : key === "color" ? 360 : 320,
            monitorHeight: 540, monitorUserResized: false,
            auxiliaryDockWidth: 340, bottomDockWidth: 340,
            timelineHeight: key === "color" ? 280 : 320,
            sourceTab: "media", inspectorTab: key === "color" ? "color" : "controls",
            scopesVisible: key === "color", scopesDetached: false,
            scopeKind: "waveform", curveDocked: true, curveOpen: false,
            dockPanels: defaultDockPanels(key),
            activePanelByDock: defaultActivePanelByDock(key)
        }
    }

    function normalized(key, value) {
        const fallback = defaults(key)
        if (!value || typeof value !== "object" || Array.isArray(value))
            return fallback
        const result = Object.assign({}, fallback)
        for (const name of ["sourceCollapsed", "sourceHidden", "inspectorCollapsed",
                            "sourceDetached", "inspectorDetached", "monitorUserResized",
                            "scopesVisible", "scopesDetached", "curveDocked", "curveOpen"]) {
            if (typeof value[name] === "boolean")
                result[name] = value[name]
        }
        result.sourceWidth = bounded(value.sourceWidth, fallback.sourceWidth, 260, 560)
        result.inspectorWidth = bounded(value.inspectorWidth, fallback.inspectorWidth, 280, 560)
        result.monitorHeight = bounded(value.monitorHeight, fallback.monitorHeight, 180, 1800)
        result.auxiliaryDockWidth = bounded(value.auxiliaryDockWidth, fallback.auxiliaryDockWidth, 280, 760)
        result.bottomDockWidth = bounded(value.bottomDockWidth, fallback.bottomDockWidth, 320, 960)
        result.timelineHeight = bounded(value.timelineHeight, fallback.timelineHeight, 180, 800)
        if (["media", "source"].indexOf(value.sourceTab) >= 0)
            result.sourceTab = value.sourceTab
        if (["controls", "effects", "motion", "animate", "audio", "transitions", "clip", "color", "title"].indexOf(value.inspectorTab) >= 0)
            result.inspectorTab = value.inspectorTab
        if (["waveform", "vectorscope", "parade", "histogram", "phase"].indexOf(value.scopeKind) >= 0)
            result.scopeKind = value.scopeKind
        result.dockPanels = normalizeDockPanels(value.dockPanels, key)
        result.activePanelByDock = normalizeActivePanelByDock(value.activePanelByDock, result.dockPanels, key)
        return result
    }

    function restore(serialized) {
        let parsed = ({})
        try { parsed = JSON.parse(String(serialized || "{}")) } catch (error) { }
        const next = ({})
        if (parsed && typeof parsed === "object" && !Array.isArray(parsed)) {
            for (const key of ["edit", "color", "audio", "finish"]) {
                if (Object.prototype.hasOwnProperty.call(parsed, key))
                    next[key] = normalized(key, parsed[key])
            }
        }
        layouts = next
    }

    function layout(key) {
        return normalized(key, layouts[key])
    }

    // Bring older saved Edit workspaces into the left Controls / right Effects
    // arrangement while retaining their dimensions and unrelated panel choices.
    function migrateEditingControlsLayout() {
        const editing = layout("edit")
        const nextPanels = Object.assign({}, editing.dockPanels)
        for (const panelId of panelIds)
            nextPanels[panelId] = Object.assign({}, nextPanels[panelId])

        nextPanels.media.dock = "left"
        nextPanels.media.returnDock = "left"
        nextPanels.media.visible = true
        nextPanels.inspector.dock = "left"
        nextPanels.inspector.returnDock = "left"
        nextPanels.inspector.visible = true
        nextPanels.effects.dock = "right"
        nextPanels.effects.returnDock = "right"
        nextPanels.effects.visible = true

        function orderDock(dockId, firstIds) {
            const remaining = panelIds.filter(function(panelId) {
                return nextPanels[panelId].dock === dockId && firstIds.indexOf(panelId) < 0
            })
            remaining.sort(function(first, second) {
                const difference = nextPanels[first].order - nextPanels[second].order
                return difference !== 0 ? difference : panelIds.indexOf(first) - panelIds.indexOf(second)
            })
            const ordered = firstIds.concat(remaining)
            for (let index = 0; index < ordered.length; ++index)
                nextPanels[ordered[index]].order = index
        }
        orderDock("left", ["media", "inspector"])
        orderDock("right", ["effects"])

        const migrated = Object.assign({}, editing, {
            sourceWidth: Math.max(320, editing.sourceWidth),
            dockPanels: nextPanels,
            activePanelByDock: Object.assign({}, editing.activePanelByDock, {
                left: "inspector", right: "effects"
            })
        })
        remember("edit", migrated)
        return layout("edit")
    }

    function migrateLegacyEditingKeyframes() {
        const editing = layout("edit")
        if (editing.inspectorTab !== "effects" || editing.curveDocked === false
                || editing.sourceDetached || editing.inspectorDetached)
            return false
        const panels = editing.dockPanels
        const defaults = defaultDockPanels("edit")
        for (let index = 0; index < panelIds.length; ++index) {
            const id = panelIds[index]
            const panel = panels[id]
            if (!panel || panel.dock === "floating")
                return false
            if (id === "keyframes") {
                if (!panel.visible || panel.dock !== "bottom")
                    return false
            } else if (panel.visible !== defaults[id].visible
                       || panel.dock !== defaults[id].dock) {
                return false
            }
        }
        const active = editing.activePanelByDock || ({})
        if (String(active.left || "") !== "inspector"
                || String(active.right || "") !== "effects"
                || String(active.bottom || "") !== "keyframes")
            return false

        const nextPanels = Object.assign({}, panels)
        for (const id of panelIds)
            nextPanels[id] = Object.assign({}, nextPanels[id])
        nextPanels.keyframes.visible = false
        nextPanels.keyframes.dock = "right"
        nextPanels.keyframes.returnDock = "right"
        const migrated = Object.assign({}, editing, {
            dockPanels: nextPanels,
            activePanelByDock: Object.assign({}, active, {
                left: "inspector", right: "effects", bottom: ""
            }),
            curveOpen: false,
            curveDocked: true,
            inspectorTab: "controls"
        })
        remember("edit", migrated)
        return true
    }

    // One complete editing workspace. Keep saved dimensions and optional tools,
    // while placing the primary tools where editors can always find their tabs.
    function migrateCompleteEditingLayout() {
        const editing = layout("edit")
        const panels = ({})
        for (const id of panelIds)
            panels[id] = Object.assign({}, editing.dockPanels[id])
        const groups = {left: ["media", "inspector", "mixer"], right: ["effects", "color", "titles"]}
        for (const dock of ["left", "right"]) {
            const primary = groups[dock]
            for (let index = 0; index < primary.length; ++index) {
                const panel = panels[primary[index]]
                panel.visible = true
                panel.dock = dock
                panel.returnDock = dock
                panel.order = index
            }
            const remaining = panelIds.filter(function(id) {
                return panels[id].dock === dock && primary.indexOf(id) < 0
            }).sort(function(a, b) { return panels[a].order - panels[b].order })
            for (let index = 0; index < remaining.length; ++index)
                panels[remaining[index]].order = primary.length + index
        }
        remember("edit", Object.assign({}, editing, {
            sourceCollapsed: false, sourceHidden: false, inspectorCollapsed: false,
            sourceWidth: Math.max(400, editing.sourceWidth), inspectorTab: "controls",
            dockPanels: panels,
            activePanelByDock: Object.assign({}, editing.activePanelByDock, {
                left: "inspector", right: "effects", bottom: ""
            })
        }))
        return layout("edit")
    }

    function remember(key, value) {
        if (!validPreset(key))
            return false
        const next = Object.assign({}, layouts)
        next[key] = normalized(key, value)
        layouts = next
        return true
    }

    function reset(key) {
        if (!validPreset(key))
            return false
        const next = Object.assign({}, layouts)
        delete next[key]
        layouts = next
        return true
    }
}
