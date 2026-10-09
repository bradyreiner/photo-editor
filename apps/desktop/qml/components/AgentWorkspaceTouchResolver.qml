import QtQuick

// Shared mapping from structured agent activity to controls in the live
// workspace. Both StudioWorkspace and the focused native motion test use this
// implementation so tests exercise the production selection/clipping rules.
QtObject {
    function findVisibleControl(item, name, label) {
        if (!item || !item.visible)
            return null
        if ((name.length > 0 && String(item.objectName || "") === name) ||
                (label.length > 0 && String(item.label || "") === label))
            return item
        const children = item.children || []
        for (let index = 0; index < children.length; ++index) {
            const found = findVisibleControl(children[index], name, label)
            if (found)
                return found
        }
        return null
    }

    function resolve(event, parameterKey, controlTree, overlay, fallbackViewport, boundsItem) {
        const currentEvent = event || ({})
        const key = String(parameterKey || "")
        const anchor = String(currentEvent.uiAnchor || "image")
        const stage = String(currentEvent.stage || "").trim().toLowerCase()
        const evidence = currentEvent.evidence || ({})
        const operation = String(evidence.operation || "").trim().toLowerCase()
        const toolName = String(evidence.toolName || "").trim().toLowerCase()
        const combinedOperation = operation + " " + toolName
        const renderProof = ["render", "preview", "validate", "proof", "model-review"].indexOf(stage) >= 0 ||
                            operation.indexOf("render") >= 0 || operation.indexOf("proof") >= 0
        const textOperation = combinedOperation.indexOf("text") >= 0 ||
                              combinedOperation.indexOf("typography") >= 0
        const effectLayerOperation = ["add_effect", "update_effect", "remove_effect", "reorder_effect",
                                      "mask_effect", "clear_effect_mask", "copy_selection"].indexOf(operation) >= 0 ||
                                     operation.indexOf("layer") >= 0 || toolName.indexOf("layer") >= 0
        const paintOperation = operation.indexOf("paint") >= 0 || operation.indexOf("brush") >= 0 ||
                               toolName.indexOf("paint") >= 0 || toolName.indexOf("brush") >= 0
        const selectionOperation = !effectLayerOperation &&
                                   (operation.indexOf("mask") >= 0 || operation.indexOf("selection") >= 0 ||
                                    operation.indexOf("lasso") >= 0 || operation.indexOf("select_") === 0 ||
                                    toolName.indexOf("mask") >= 0 || toolName.indexOf("selection") >= 0)
        const labels = { exposure: "Exposure", contrast: "Contrast", highlights: "Highlights",
            shadows: "Shadows", whites: "Whites", blacks: "Blacks", temperature: "Temperature",
            tint: "Tint", saturation: "Saturation", vibrance: "Vibrance", clarity: "Clarity",
            texture: "Texture", dehaze: "Dehaze", brightness: "Brightness" }
        let control = null
        if (key.length > 0) {
            const label = labels[key] || ""
            if (label.length === 0)
                return null
            control = findVisibleControl(controlTree, key === "exposure" ? "exposureSlider" : "", label)
        } else if (renderProof) {
            // Proof reports describe pixels already produced; they are not
            // adjustment gestures. Only an explicit compare anchor maps to a
            // live control.
            if (anchor === "compare")
                control = findVisibleControl(controlTree, "editRailCompareButton", "")
        } else if (textOperation) {
            control = findVisibleControl(controlTree, "editRailTextButton", "")
        } else if (effectLayerOperation) {
            control = findVisibleControl(controlTree, "layersInspectorTabButton", "")
        } else if (paintOperation) {
            control = findVisibleControl(controlTree, "editRailPaintButton", "")
        } else if (selectionOperation) {
            control = findVisibleControl(controlTree, "editRailSelectionButton", "")
        } else {
            const name = operation.indexOf("select") === 0 ? "editRailSelectionButton"
                       : anchor === "inspector" ? "adjustInspectorTabButton"
                       : anchor === "compare" ? "editRailCompareButton" : ""
            if (name.length > 0)
                control = findVisibleControl(controlTree, name, "")
        }

        if (control) {
            const local = control.agentContactPoint || ({x: control.width / 2, y: control.height / 2})
            const point = control.mapToItem(overlay, local.x, local.y)
            let ancestor = control.parent
            while (ancestor) {
                if (ancestor.clip) {
                    const clipped = overlay.mapToItem(ancestor, point.x, point.y)
                    if (clipped.x < 0 || clipped.x > ancestor.width ||
                            clipped.y < 0 || clipped.y > ancestor.height)
                        return null
                }
                ancestor = ancestor.parent
            }
            if (point.x < 0 || point.x > boundsItem.width ||
                    point.y < 0 || point.y > boundsItem.height)
                return null
            const localSurface = control.agentSurfaceRect || ({x: 0, y: 0, width: control.width, height: 0})
            const surface = control.mapToItem(overlay, localSurface.x, localSurface.y)
            let panel = control
            let parent = control.parent
            while (parent) {
                if (String(parent.objectName || "") === "editInspectorPanel" ||
                        String(parent.objectName || "").toLowerCase().indexOf("toolrail") >= 0) {
                    panel = parent
                    break
                }
                parent = parent.parent
            }
            const panelOrigin = panel.mapToItem(overlay, 0, 0)
            const targetLabel = labels[key] || (textOperation ? "Text"
                              : effectLayerOperation ? "Layers"
                              : paintOperation ? "Paint"
                              : selectionOperation ? "Selection"
                              : String(operation || anchor).replace(/_/g, " "))
            return {x: point.x, y: point.y, label: targetLabel,
                    interactive: true, controlName: String(control.objectName || ""),
                    surfaceRect: {x: surface.x, y: surface.y, width: localSurface.width},
                    panelRect: {x: panelOrigin.x, y: panelOrigin.y, width: panel.width, height: panel.height}}
        }
        if (key.length > 0)
            return null
        if (fallbackViewport) {
            const origin = fallbackViewport.mapToItem(overlay, 0, 0)
            const bottom = origin.y + fallbackViewport.height
            return {x: origin.x + fallbackViewport.width * 0.62, y: bottom, label: "Photo", interactive: false,
                    surfaceRect: {x: origin.x, y: bottom, width: fallbackViewport.width},
                    panelRect: {x: origin.x, y: origin.y, width: fallbackViewport.width, height: fallbackViewport.height}}
        }
        return null
    }
}
