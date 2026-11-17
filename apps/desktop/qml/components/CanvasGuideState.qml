import QtQuick
import QtCore

QtObject {
    id: root

    // Guides are local workspace aids, keyed by document, and never enter renders.
    property string workspaceKey: "Edit"
    property string documentKey: ""
    property real canvasWidth: 1
    property real canvasHeight: 1
    property alias unit: preferences.unit
    property alias pixelsPerInch: preferences.pixelsPerInch
    property alias rulersVisible: preferences.rulersVisible
    property alias guidesVisible: preferences.guidesVisible
    property alias snappingEnabled: preferences.snappingEnabled
    property alias locked: preferences.locked
    property Settings storage: Settings {
        id: preferences
        category: "CanvasRulersAndGuides-" + root.workspaceKey
        property string unit: "px"
        property real pixelsPerInch: 96
        property bool rulersVisible: true
        property bool guidesVisible: true
        property bool snappingEnabled: true
        property bool locked: false
        property string documentsJson: "{}"
    }
    readonly property var documents: {
        try {
            return JSON.parse(preferences.documentsJson) || ({});
        } catch (error) {
            return ({});
        }
    }
    readonly property var guides: documentKey.length > 0 ? (documents[documentKey] || []) : []

    function pixelsPerUnit(vertical) {
        if (unit === "in")
            return Math.max(1, pixelsPerInch);
        if (unit === "cm")
            return Math.max(1, pixelsPerInch) / 2.54;
        if (unit === "%")
            return Math.max(1, vertical ? canvasWidth : canvasHeight) / 100;
        return 1;
    }

    function quantize(position, vertical, shift) {
        const step = 5 * pixelsPerUnit(vertical);
        return shift ? Math.round(position / step) * step : position;
    }

    function label(position, vertical) {
        const value = position / pixelsPerUnit(vertical);
        return Number(value.toFixed(unit === "px" || unit === "%" ? 1 : 2)) + " " + unit;
    }

    function commitGuide(index, axis, position, remove) {
        if (locked || !documentKey.length || !Number.isFinite(position))
            return;
        const next = guides.slice();
        if (index >= 0)
            next.splice(index, 1);
        if (!remove)
            next.push({
                axis: axis,
                position: position
            });
        const records = Object.assign({}, documents);
        records[documentKey] = next;
        preferences.documentsJson = JSON.stringify(records);
    }

    function clear() {
        if (locked || !documentKey.length)
            return;
        const records = Object.assign({}, documents);
        delete records[documentKey];
        preferences.documentsJson = JSON.stringify(records);
    }

    function snapTargets(vertical) {
        if (!guidesVisible || !snappingEnabled)
            return [];
        return guides.filter(function (guide) {
            return guide.axis === (vertical ? "x" : "y");
        }).map(function (guide) {
            return {
                position: guide.position,
                label: "Guide · " + root.label(guide.position, vertical)
            };
        });
    }

    // Return one correction for the closest edge or center within a screen-space tolerance.
    function snapDelta(anchors, vertical, screenScale, tolerance) {
        let delta = 0;
        let distance = tolerance / Math.max(0.000001, screenScale);
        for (const target of snapTargets(vertical)) {
            for (const anchor of anchors) {
                const candidate = target.position - anchor;
                if (Math.abs(candidate) <= distance) {
                    distance = Math.abs(candidate);
                    delta = candidate;
                }
            }
        }
        return delta;
    }

    function snapItemDelta(item, viewport, dx, dy, bypass) {
        if (bypass)
            return Qt.point(dx, dy);
        const origin = item.parent.mapToItem(viewport, 0, 0);
        const moved = item.parent.mapToItem(viewport, dx, dy);
        const sx = viewport.width / Math.max(1, canvasWidth);
        const sy = viewport.height / Math.max(1, canvasHeight);
        const corners = [item.mapToItem(viewport, 0, 0), item.mapToItem(viewport, item.width, 0), item.mapToItem(viewport, 0, item.height), item.mapToItem(viewport, item.width, item.height)];
        const xs = corners.map(function (point) {
            return point.x;
        });
        const ys = corners.map(function (point) {
            return point.y;
        });
        const left = Math.min.apply(null, xs), right = Math.max.apply(null, xs);
        const top = Math.min.apply(null, ys), bottom = Math.max.apply(null, ys);
        const deltaX = moved.x - origin.x, deltaY = moved.y - origin.y;
        const correctionX = snapDelta([(left + deltaX) / sx, ((left + right) / 2 + deltaX) / sx, (right + deltaX) / sx], true, sx, 6) * sx;
        const correctionY = snapDelta([(top + deltaY) / sy, ((top + bottom) / 2 + deltaY) / sy, (bottom + deltaY) / sy], false, sy, 6) * sy;
        const corrected = item.parent.mapFromItem(viewport, moved.x + correctionX, moved.y + correctionY);
        return Qt.point(corrected.x, corrected.y);
    }
}
