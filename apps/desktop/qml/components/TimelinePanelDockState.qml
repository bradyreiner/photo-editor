import QtQml

QtObject {
    id: root

    readonly property var dockIds: ["left", "right", "auxiliary", "bottom", "floating"]
    readonly property var panelDefinitions: [
        { "id": "media", "label": "Library", "dock": "left" },
        { "id": "source", "label": "Source", "dock": "left" },
        { "id": "effects", "label": "Effects", "dock": "right" },
        { "id": "inspector", "label": "Controls", "dock": "left" },
        { "id": "color", "label": "Color", "dock": "right" },
        { "id": "mixer", "label": "Audio", "dock": "left" },
        { "id": "keyframes", "label": "Keyframes", "dock": "right" },
        { "id": "titles", "label": "Titles", "dock": "right" },
        { "id": "captions", "label": "Captions", "dock": "bottom" },
        { "id": "transcript", "label": "Transcript", "dock": "bottom" },
        { "id": "scopes", "label": "Scopes", "dock": "auxiliary" },
        { "id": "review", "label": "Notes", "dock": "right" }
    ]
    readonly property var panelIds: panelDefinitions.map(function(panel) { return panel.id })

    property string activePreset: "edit"
    property var panelState: defaultPanelState("edit")
    property var activePanelByDock: defaultActivePanels("edit")
    property var savedPresets: ({})

    // Live hosts and pointer state are deliberately absent from snapshots.
    property var dockHosts: []
    property string draggedPanelId: ""
    property string dragTargetDock: ""
    property int dragTargetIndex: -1
    property var panelDropHandler: null

    function registerDockHost(host) {
        if (dockHosts.indexOf(host) < 0) dockHosts = dockHosts.concat([host])
    }
    function unregisterDockHost(host) {
        dockHosts = dockHosts.filter(function(entry) { return entry !== host })
    }
    function beginPanelDrag(panelId) {
        if (!validPanel(panelId)) return false
        draggedPanelId = panelId
        dragTargetDock = ""
        dragTargetIndex = -1
        return true
    }
    function updatePanelDrag(globalX, globalY) {
        dragTargetDock = ""
        dragTargetIndex = -1
        for (const host of dockHosts) {
            if (!host || !host.visible || host.dockId === "floating") continue
            const point = host.mapFromGlobal(globalX, globalY)
            if (point.x >= 0 && point.y >= 0 && point.x < host.width && point.y < host.height) {
                dragTargetDock = host.dockId
                dragTargetIndex = host.dropIndexAt(globalX, globalY)
                break
            }
        }
    }
    function finishPanelDrag(globalX, globalY) {
        const panelId = draggedPanelId
        updatePanelDrag(globalX, globalY)
        const destination = dragTargetDock
        const index = dragTargetIndex
        cancelPanelDrag()
        if (!panelId) return false
        if (destination) return movePanel(panelId, destination, index)
        if (panelDropHandler && panelDropHandler(panelId, globalX, globalY)) return true
        return floatPanel(panelId, {x: globalX - 40, y: globalY - 20, width: 460, height: 560})
    }
    function cancelPanelDrag() {
        draggedPanelId = ""
        dragTargetDock = ""
        dragTargetIndex = -1
    }

    signal panelFocused(string panelId, string dockId)
    signal panelVisibilityChanged(string panelId, bool visible)
    signal panelPlacementChanged(string panelId, string dockId, int index)
    signal panelFloatingChanged(string panelId, bool floating)
    signal presetApplied(string presetId)

    function validPanel(panelId) {
        return panelIds.indexOf(String(panelId || "")) >= 0
    }

    function validDock(dockId) {
        return dockIds.indexOf(String(dockId || "")) >= 0
    }

    function plainObject(value) {
        return value !== null && typeof value === "object" && !Array.isArray(value)
    }

    function bounded(value, fallback, low, high, integer) {
        const number = Number(value)
        if (!isFinite(number))
            return fallback
        const clamped = Math.max(low, Math.min(high, number))
        return integer ? Math.round(clamped) : clamped
    }

    function defaultPanelState(presetId) {
        const key = ["edit", "color", "audio", "finish"].indexOf(String(presetId || "")) >= 0
                ? String(presetId) : "edit"
        const visibility = {
            media: key !== "color",
            source: false,
            effects: key === "edit",
            inspector: key === "edit" || key === "finish",
            color: key === "edit" || key === "color",
            mixer: key === "edit" || key === "audio",
            keyframes: false,
            titles: key === "edit",
            captions: key === "finish",
            transcript: false,
            scopes: key === "color",
            review: false
        }
        const placements = defaultPanelPlacements(key)
        const result = ({})
        for (let index = 0; index < panelDefinitions.length; ++index) {
            const id = panelDefinitions[index].id
            result[id] = {
                visible: visibility[id] === true,
                dock: placements[id],
                order: 0,
                returnDock: placements[id],
                floatingGeometry: defaultFloatingGeometry(id)
            }
        }
        return normalizePanelState(result, key)
    }

    function defaultPanelPlacements(presetId) {
        const key = String(presetId || "edit")
        if (key === "edit") {
            return {
                media: "left", source: "left", inspector: "left", effects: "right",
                color: "right", scopes: "auxiliary", mixer: "left", keyframes: "right",
                titles: "right", captions: "bottom", transcript: "bottom", review: "right"
            }
        }
        if (key === "color") {
            return {
                media: "left", source: "left", effects: "auxiliary", inspector: "right",
                color: "right", scopes: "bottom", mixer: "bottom", keyframes: "bottom",
                titles: "bottom", captions: "bottom", transcript: "bottom", review: "right"
            }
        }
        if (key === "audio") {
            return {
                media: "left", source: "left", effects: "right", inspector: "right",
                color: "auxiliary", scopes: "right", mixer: "bottom", keyframes: "bottom",
                titles: "bottom", captions: "bottom", transcript: "bottom", review: "right"
            }
        }
        if (key === "finish") {
            return {
                media: "left", source: "left", effects: "right", inspector: "right",
                color: "auxiliary", scopes: "right", mixer: "bottom", keyframes: "bottom",
                titles: "bottom", captions: "bottom", transcript: "bottom", review: "right"
            }
        }
        return {
            media: "left", source: "left", effects: "right", inspector: "right",
            color: "right", scopes: "auxiliary", mixer: "bottom", keyframes: "bottom",
            titles: "bottom", captions: "bottom", transcript: "bottom", review: "right"
        }
    }

    function defaultFloatingGeometry(panelId) {
        const wider = ["media", "mixer", "transcript"].indexOf(String(panelId)) >= 0
        return { x: 120, y: 90, width: wider ? 520 : 420, height: wider ? 420 : 360 }
    }

    function defaultActivePanels(presetId) {
        const key = ["edit", "color", "audio", "finish"].indexOf(String(presetId || "")) >= 0
                ? String(presetId) : "edit"
        if (key === "color")
            return { left: "", right: "color", auxiliary: "", bottom: "scopes", floating: "" }
        if (key === "audio")
            return { left: "media", right: "", auxiliary: "", bottom: "mixer", floating: "" }
        if (key === "finish")
            return { left: "media", right: "inspector", auxiliary: "", bottom: "captions", floating: "" }
        return { left: "inspector", right: "effects", auxiliary: "", bottom: "", floating: "" }
    }

    function normalizeGeometry(value, panelId) {
        const fallback = defaultFloatingGeometry(panelId)
        if (!plainObject(value))
            return fallback
        return {
            x: bounded(value.x, fallback.x, -10000, 10000, true),
            y: bounded(value.y, fallback.y, -10000, 10000, true),
            width: bounded(value.width, fallback.width, 220, 1600, true),
            height: bounded(value.height, fallback.height, 180, 1200, true)
        }
    }

    function normalizePanelState(value, presetId) {
        const fallback = defaultPanelStateFallback(presetId)
        if (!plainObject(value))
            return fallback
        const result = ({})
        for (let index = 0; index < panelIds.length; ++index) {
            const id = panelIds[index]
            const fallbackPanel = fallback[id]
            const saved = value[id]
            const panel = plainObject(saved) ? saved : ({})
            result[id] = {
                visible: typeof panel.visible === "boolean" ? panel.visible : fallbackPanel.visible,
                dock: validDock(panel.dock) ? String(panel.dock) : fallbackPanel.dock,
                order: bounded(panel.order, fallbackPanel.order, 0, 64, true),
                returnDock: validDock(panel.returnDock) && panel.returnDock !== "floating"
                            ? String(panel.returnDock) : fallbackPanel.returnDock,
                floatingGeometry: normalizeGeometry(panel.floatingGeometry, id)
            }
        }
        normalizeOrders(result)
        return result
    }

    function legacyEditingPanelArrangement(panelsValue, activeValue) {
        if (!plainObject(panelsValue))
            return null
        const panels = normalizePanelState(panelsValue, "edit")
        const defaults = defaultPanelState("edit")
        for (let index = 0; index < panelIds.length; ++index) {
            const id = panelIds[index]
            const panel = panels[id]
            if (panel.dock === "floating")
                return null
            if (id === "keyframes") {
                if (!panel.visible || panel.dock !== "bottom")
                    return null
            } else if (panel.visible !== defaults[id].visible
                       || panel.dock !== defaults[id].dock) {
                return null
            }
        }
        const active = normalizeActivePanels(activeValue, panels, "edit")
        if (active.left !== "inspector" || active.right !== "effects"
                || active.bottom !== "keyframes")
            return null

        const migrated = clonePanels(panels)
        migrated.keyframes.visible = false
        migrated.keyframes.dock = "right"
        migrated.keyframes.returnDock = "right"
        normalizeOrders(migrated)
        const migratedActive = normalizeActivePanels({
            left: "inspector", right: "effects", auxiliary: "", bottom: "", floating: ""
        }, migrated, "edit")
        return { panels: migrated, activePanelByDock: migratedActive }
    }

    function migrateLegacyEditingKeyframes() {
        let migratedAny = false
        if (activePreset === "edit") {
            const migrated = legacyEditingPanelArrangement(panelState, activePanelByDock)
            if (migrated !== null) {
                panelState = migrated.panels
                activePanelByDock = migrated.activePanelByDock
                migratedAny = true
            }
        }

        const editPreset = plainObject(savedPresets) ? savedPresets.edit : null
        if (plainObject(editPreset)) {
            const migratedPreset = legacyEditingPanelArrangement(
                        editPreset.panels || editPreset.dockPanels,
                        editPreset.activePanelByDock)
            if (migratedPreset !== null) {
                const nextPresets = Object.assign({}, savedPresets)
                nextPresets.edit = {
                    panels: migratedPreset.panels,
                    activePanelByDock: migratedPreset.activePanelByDock,
                    activePreset: "edit"
                }
                savedPresets = nextPresets
                migratedAny = true
            }
        }
        return migratedAny
    }

    function defaultPanelStateFallback(presetId) {
        const key = ["edit", "color", "audio", "finish"].indexOf(String(presetId || "")) >= 0
                ? String(presetId) : "edit"
        const placements = defaultPanelPlacements(key)
        const defaults = {
            media: { visible: key !== "color", dock: "left", order: 0 },
            source: { visible: false, dock: "left", order: 1 },
            effects: { visible: key === "edit", dock: placements.effects, order: 0 },
            inspector: { visible: key === "edit" || key === "finish", dock: placements.inspector, order: 1 },
            color: { visible: key === "color", dock: placements.color, order: 0 },
            mixer: { visible: key === "audio", dock: "bottom", order: 0 },
            keyframes: { visible: false, dock: placements.keyframes, order: 1 },
            titles: { visible: false, dock: "bottom", order: 2 },
            captions: { visible: key === "finish", dock: "bottom", order: 3 },
            transcript: { visible: false, dock: "bottom", order: 4 },
            scopes: { visible: key === "color", dock: placements.scopes, order: 1 },
            review: { visible: false, dock: placements.review, order: 2 }
        }
        const result = ({})
        for (let index = 0; index < panelIds.length; ++index) {
            const id = panelIds[index]
            const panel = defaults[id]
            result[id] = {
                visible: panel.visible,
                dock: panel.dock,
                order: panel.order,
                returnDock: panel.dock,
                floatingGeometry: defaultFloatingGeometry(id)
            }
        }
        return result
    }

    function normalizeOrders(panels) {
        for (let dockIndex = 0; dockIndex < dockIds.length; ++dockIndex) {
            const dockId = dockIds[dockIndex]
            const members = panelIds.filter(function(id) { return panels[id].dock === dockId })
            members.sort(function(first, second) {
                const delta = panels[first].order - panels[second].order
                return delta !== 0 ? delta : panelIds.indexOf(first) - panelIds.indexOf(second)
            })
            for (let index = 0; index < members.length; ++index)
                panels[members[index]].order = index
        }
        return panels
    }

    function normalizeActivePanels(value, panels, presetId) {
        const fallback = defaultActivePanels(presetId)
        const result = ({})
        for (let dockIndex = 0; dockIndex < dockIds.length; ++dockIndex) {
            const dockId = dockIds[dockIndex]
            const savedId = plainObject(value) ? String(value[dockId] || "") : ""
            if (validPanel(savedId) && panels[savedId].dock === dockId && panels[savedId].visible)
                result[dockId] = savedId
            else if (validPanel(fallback[dockId]) && panels[fallback[dockId]].dock === dockId && panels[fallback[dockId]].visible)
                result[dockId] = fallback[dockId]
            else
                result[dockId] = firstVisiblePanel(panels, dockId)
        }
        return result
    }

    function firstVisiblePanel(panels, dockId) {
        const members = panelIds.filter(function(id) { return panels[id].dock === dockId && panels[id].visible })
        members.sort(function(first, second) { return panels[first].order - panels[second].order })
        return members.length > 0 ? members[0] : ""
    }

    function normalizedSnapshot(value, presetId) {
        const key = ["edit", "color", "audio", "finish"].indexOf(String(presetId || "")) >= 0
                ? String(presetId) : "edit"
        const source = plainObject(value) ? value : ({})
        const rawPanels = plainObject(source.panels) ? source.panels
                          : plainObject(source.dockPanels) ? source.dockPanels : ({})
        const panels = normalizePanelState(rawPanels, key)
        return {
            panels: panels,
            activePanelByDock: normalizeActivePanels(source.activePanelByDock, panels, key),
            activePreset: key
        }
    }

    function visible(panelId) {
        return validPanel(panelId) && panelState[panelId].visible === true
    }

    function dock(panelId) {
        return validPanel(panelId) ? panelState[panelId].dock : ""
    }

    function order(panelId) {
        return validPanel(panelId) ? Number(panelState[panelId].order) : -1
    }

    function activePanel(dockId) {
        return validDock(dockId) ? String(activePanelByDock[dockId] || "") : ""
    }

    function primaryPanelForPreset(presetId) {
        const primaries = { edit: "inspector", color: "color", audio: "mixer", finish: "captions" }
        return String(primaries[String(presetId || "")] || "")
    }

    function panelsInDock(dockId, visibleOnly) {
        if (!validDock(dockId))
            return []
        const onlyVisible = visibleOnly !== false
        const members = panelIds.filter(function(id) {
            return panelState[id].dock === dockId && (!onlyVisible || panelState[id].visible)
        })
        members.sort(function(first, second) {
            const delta = panelState[first].order - panelState[second].order
            return delta !== 0 ? delta : panelIds.indexOf(first) - panelIds.indexOf(second)
        })
        return members
    }

    function focusPanel(panelId) {
        const id = String(panelId || "")
        if (!validPanel(id))
            return false
        const nextPanels = clonePanels(panelState)
        const wasVisible = nextPanels[id].visible
        nextPanels[id].visible = true
        panelState = nextPanels
        const nextActive = Object.assign({}, activePanelByDock)
        nextActive[nextPanels[id].dock] = id
        activePanelByDock = nextActive
        if (!wasVisible)
            panelVisibilityChanged(id, true)
        panelFocused(id, nextPanels[id].dock)
        return true
    }

    function setVisible(panelId, shouldBeVisible) {
        const id = String(panelId || "")
        if (!validPanel(id) || typeof shouldBeVisible !== "boolean")
            return false
        if (shouldBeVisible)
            return focusPanel(id)
        const nextPanels = clonePanels(panelState)
        const dockId = nextPanels[id].dock
        if (!nextPanels[id].visible)
            return true
        nextPanels[id].visible = false
        panelState = nextPanels
        const nextActive = Object.assign({}, activePanelByDock)
        if (nextActive[dockId] === id)
            nextActive[dockId] = firstVisiblePanel(nextPanels, dockId)
        activePanelByDock = nextActive
        panelVisibilityChanged(id, false)
        return true
    }

    function togglePanel(panelId) {
        const id = String(panelId || "")
        return validPanel(id) ? setVisible(id, !visible(id)) : false
    }

    function movePanel(panelId, nextDock, index) {
        const id = String(panelId || "")
        const destination = String(nextDock || "")
        if (!validPanel(id) || !validDock(destination))
            return false
        const nextPanels = clonePanels(panelState)
        const previousDock = nextPanels[id].dock
        const wasFloating = previousDock === "floating"
        const wasVisible = nextPanels[id].visible
        if (destination === "floating" && previousDock !== "floating")
            nextPanels[id].returnDock = previousDock
        else if (destination !== "floating")
            nextPanels[id].returnDock = destination
        const requestedIndex = bounded(index, 999, 0, panelIds.length, true)
        nextPanels[id].dock = destination
        const members = panelIds.filter(function(otherId) {
            return otherId !== id && nextPanels[otherId].dock === destination
        })
        members.sort(function(first, second) { return nextPanels[first].order - nextPanels[second].order })
        const insertionIndex = Math.min(requestedIndex, members.length)
        members.splice(insertionIndex, 0, id)
        for (let position = 0; position < members.length; ++position)
            nextPanels[members[position]].order = position
        nextPanels[id].visible = true
        normalizeOrders(nextPanels)
        panelState = nextPanels

        const nextActive = Object.assign({}, activePanelByDock)
        if (nextActive[previousDock] === id)
            nextActive[previousDock] = firstVisiblePanel(nextPanels, previousDock)
        nextActive[destination] = id
        activePanelByDock = nextActive
        panelPlacementChanged(id, destination, nextPanels[id].order)
        if (wasFloating !== (destination === "floating"))
            panelFloatingChanged(id, destination === "floating")
        if (!wasVisible)
            panelVisibilityChanged(id, true)
        panelFocused(id, destination)
        return true
    }

    function floatPanel(panelId, geometry) {
        const id = String(panelId || "")
        if (!validPanel(id))
            return false
        if (plainObject(geometry)) {
            const nextPanels = clonePanels(panelState)
            nextPanels[id].floatingGeometry = normalizeGeometry(geometry, id)
            panelState = nextPanels
        }
        return movePanel(id, "floating", 999)
    }

    function setFloatingGeometry(panelId, geometry) {
        const id = String(panelId || "")
        if (!validPanel(id) || !plainObject(geometry))
            return false
        const nextPanels = clonePanels(panelState)
        nextPanels[id].floatingGeometry = normalizeGeometry(geometry, id)
        panelState = nextPanels
        return true
    }

    function dockPanel(panelId, dockId, index) {
        const id = String(panelId || "")
        let destination = String(dockId || "")
        if (!validPanel(id))
            return false
        if (!validDock(destination) || destination === "floating") {
            const currentDock = panelState[id].dock
            if (currentDock === "floating" && validDock(panelState[id].returnDock)) {
                destination = panelState[id].returnDock
            } else {
                const definition = panelDefinitions.filter(function(panel) { return panel.id === id })[0]
                destination = definition ? definition.dock : "right"
            }
        }
        return movePanel(id, destination, index)
    }

    function clonePanels(source) {
        const result = ({})
        for (let index = 0; index < panelIds.length; ++index) {
            const id = panelIds[index]
            const panel = source[id]
            result[id] = {
                visible: panel.visible === true,
                dock: validDock(panel.dock) ? panel.dock : "right",
                order: Number(panel.order) || 0,
                returnDock: validDock(panel.returnDock) && panel.returnDock !== "floating"
                            ? panel.returnDock
                            : (validDock(panel.dock) && panel.dock !== "floating" ? panel.dock : "right"),
                floatingGeometry: normalizeGeometry(panel.floatingGeometry, id)
            }
        }
        return result
    }

    function snapshot() {
        return {
            panels: clonePanels(panelState),
            activePanelByDock: Object.assign({}, activePanelByDock),
            activePreset: activePreset,
            savedPresets: Object.assign({}, savedPresets)
        }
    }

    function normalizeSavedPresets(value) {
        const result = ({})
        if (!plainObject(value))
            return result
        const keys = ["edit", "color", "audio", "finish"]
        for (let index = 0; index < keys.length; ++index) {
            const key = keys[index]
            if (plainObject(value[key]))
                result[key] = normalizedSnapshot(value[key], key)
        }
        return result
    }

    function restore(value) {
        let parsed = value
        if (typeof value === "string") {
            try { parsed = JSON.parse(value) } catch (error) { parsed = ({}) }
        }
        const key = plainObject(parsed) && ["edit", "color", "audio", "finish"].indexOf(String(parsed.activePreset || "")) >= 0
                ? String(parsed.activePreset) : "edit"
        const normalized = normalizedSnapshot(parsed, key)
        activePreset = normalized.activePreset
        panelState = normalized.panels
        activePanelByDock = normalized.activePanelByDock
        savedPresets = normalizeSavedPresets(plainObject(parsed) ? parsed.savedPresets : null)
        return true
    }

    function presetSnapshot(presetId) {
        const key = String(presetId || "")
        if (["edit", "color", "audio", "finish"].indexOf(key) < 0)
            return null
        return {
            panels: defaultPanelState(key),
            activePanelByDock: normalizeActivePanels(defaultActivePanels(key), defaultPanelState(key), key),
            activePreset: key
        }
    }

    function applyPreset(presetId) {
        const key = String(presetId || "")
        if (["edit", "color", "audio", "finish"].indexOf(key) < 0)
            return false
        const saved = plainObject(savedPresets) ? savedPresets[key] : null
        const arrangement = saved ? normalizedSnapshot(saved, key) : presetSnapshot(key)
        return applyArrangement(key, arrangement)
    }

    function applyArrangement(presetId, value) {
        const key = String(presetId || "")
        if (["edit", "color", "audio", "finish"].indexOf(key) < 0)
            return false
        const arrangement = normalizedSnapshot(value, key)
        // Restore this task's own visibility and active tabs. Unioning with the
        // outgoing task accumulated every panel and progressively shrank Program.
        const nextPanels = clonePanels(arrangement.panels)
        const nextActive = normalizeActivePanels(arrangement.activePanelByDock, nextPanels, key)
        activePreset = key
        panelState = nextPanels
        activePanelByDock = nextActive
        presetApplied(key)
        return true
    }

    function rememberPreset(presetId) {
        const key = String(presetId || "")
        if (["edit", "color", "audio", "finish"].indexOf(key) < 0)
            return false
        const next = Object.assign({}, savedPresets)
        next[key] = {
            panels: clonePanels(panelState),
            activePanelByDock: Object.assign({}, activePanelByDock),
            activePreset: key
        }
        savedPresets = next
        return true
    }

    function resetPreset(presetId) {
        const key = String(presetId || "")
        if (["edit", "color", "audio", "finish"].indexOf(key) < 0)
            return false
        const next = Object.assign({}, savedPresets)
        delete next[key]
        savedPresets = next
        return activePreset === key ? applyPreset(key) : true
    }
}
