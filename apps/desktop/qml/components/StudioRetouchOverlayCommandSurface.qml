import QtQuick
import QtQuick.Controls.Basic

Item {
    id: commandSurface

    required property Item workspace
    required property Item viewport
    required property var previewController

    Theme { id: theme }

    Rectangle {
        id: cloneOffsetLine
        objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneOffsetLine" : "cloneOffsetLine"
        property var sourcePoint: viewport.viewPointForImage(previewController.activeMaskCloneSourceX, previewController.activeMaskCloneSourceY)
        property var targetPoint: viewport.viewPointForImage(workspace.activeCloneTargetX(), workspace.activeCloneTargetY())
        property real sourceScreenX: sourcePoint.x
        property real sourceScreenY: sourcePoint.y
        property real targetScreenX: targetPoint.x
        property real targetScreenY: targetPoint.y
        property real offsetLength: Math.sqrt(Math.pow(targetScreenX - sourceScreenX, 2) +
                                              Math.pow(targetScreenY - sourceScreenY, 2))
        property string cloneMode: previewController.activeMaskCloneAligned ? "aligned" : "non-aligned"
        visible: workspace.page === "edit" &&
                 previewController.hasActiveMask &&
                 previewController.activeMaskToolRole === "retouch" &&
                 previewController.activeMaskRetouchMode === "clone" &&
                 previewController.activeMaskCloneSourceSet
        x: sourceScreenX
        y: sourceScreenY - height / 2
        width: Math.max(1, offsetLength)
        height: 2
        radius: 1
        z: 21
        color: theme.alpha("#7cb5ff", 0.78)
        rotation: Math.atan2(targetScreenY - sourceScreenY, targetScreenX - sourceScreenX) * 180 / Math.PI
        transformOrigin: Item.Left
    }

    Rectangle {
        id: patchSourceRegion
        objectName: viewport.objectName.length > 0 ? viewport.objectName + "PatchSourceRegion" : "patchSourceRegion"
        property real sourceX: previewController.activeMaskPatchSourceX
        property real sourceY: previewController.activeMaskPatchSourceY
        property real sourceWidth: previewController.activeMaskPatchSourceWidth
        property real sourceHeight: previewController.activeMaskPatchSourceHeight
        property var sourceRect: viewport.viewRectForImage(sourceX, sourceY, sourceWidth, sourceHeight)
        property string sourceSignature: workspace.retouchPatchSourceSignature()
        visible: workspace.page === "edit" &&
                 previewController.hasActiveMask &&
                 previewController.activeMaskToolRole === "retouch" &&
                 previewController.activeMaskRetouchMode === "patch" &&
                 previewController.activeMaskPatchSourceSet
        x: sourceRect.x
        y: sourceRect.y
        width: Math.max(10, sourceRect.width)
        height: Math.max(10, sourceRect.height)
        z: 23
        color: theme.alpha("#72cbb2", patchSourceMouse.containsMouse || patchSourceMouse.pressed ? 0.18 : 0.09)
        border.width: 2
        border.color: "#d4fff8"

        function moveRegion(normalizedX, normalizedY) {
            previewController.setActiveMaskPatchSourceRegion(
                workspace.clamp(normalizedX, 0.0, 1.0 - previewController.activeMaskPatchSourceWidth),
                workspace.clamp(normalizedY, 0.0, 1.0 - previewController.activeMaskPatchSourceHeight),
                previewController.activeMaskPatchSourceWidth,
                previewController.activeMaskPatchSourceHeight)
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: 4
            color: "transparent"
            border.width: 1
            border.color: theme.alpha(theme.ink950, 0.85)
        }

        Rectangle {
            anchors.centerIn: parent
            width: 28
            height: 2
            radius: 1
            color: "#f4f7fa"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: 28
            radius: 1
            color: "#f4f7fa"
        }

        Label {
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "PatchSourceLabel" : "patchSourceLabel"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 4
            text: "Patch source"
            color: theme.textStrong
            font.family: theme.fontMono
            font.pixelSize: 10
            padding: 4
            background: Rectangle {
                color: theme.alpha(theme.ink950, 0.82)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha("#72cbb2", 0.45)
                radius: 2
            }
        }

        MouseArea {
            id: patchSourceMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeAllCursor
            property real pressOffsetX: 0
            property real pressOffsetY: 0

            onPressed: function(mouse) {
                pressOffsetX = mouse.x
                pressOffsetY = mouse.y
            }

            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const mapped = mapToItem(viewport, mouse.x - pressOffsetX, mouse.y - pressOffsetY)
                const point = viewport.normalizedPointForViewport(mapped.x, mapped.y)
                if (!point.valid)
                    return
                patchSourceRegion.moveRegion(point.x, point.y)
            }
        }
    }

    Item {
        id: cloneTargetPreview
        objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneTargetPreview" : "cloneTargetPreview"
        property real targetX: workspace.activeCloneTargetX()
        property real targetY: workspace.activeCloneTargetY()
        property var targetPoint: viewport.viewPointForImage(targetX, targetY)
        property real cloneScale: previewController.activeMaskCloneScale
        property real cloneRotation: previewController.activeMaskCloneRotation
        property string cloneMode: previewController.activeMaskCloneAligned ? "aligned" : "non-aligned"
        visible: workspace.page === "edit" &&
                 previewController.hasActiveMask &&
                 previewController.activeMaskToolRole === "retouch" &&
                 previewController.activeMaskRetouchMode === "clone" &&
                 previewController.activeMaskCloneSourceSet
        x: targetPoint.x - width / 2
        y: targetPoint.y - height / 2
        width: workspace.clamp(
                   viewport.viewDiameterForImageSpan(
                       targetX,
                       targetY,
                       previewController.activeMaskBrushSize * previewController.activeMaskCloneScale),
                   22,
                   92)
        height: width
        z: 22

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: theme.alpha("#d4fff8", 0.08)
            border.width: 2
            border.color: "#d4fff8"
            rotation: cloneTargetPreview.cloneRotation
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.max(8, parent.width * 0.55)
            height: 2
            radius: 1
            color: "#d4fff8"
            rotation: cloneTargetPreview.cloneRotation
        }

        Label {
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneTransformBadge" : "cloneTransformBadge"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 4
            text: (previewController.activeMaskCloneAligned ? "Aligned" : "Non-aligned") + " / " +
                  previewController.activeMaskCloneScale.toFixed(2) + "x / " +
                  Math.round(previewController.activeMaskCloneRotation).toString() + " deg"
            color: theme.textStrong
            font.family: theme.fontMono
            font.pixelSize: 10
            padding: 4
            background: Rectangle {
                color: theme.alpha(theme.ink950, 0.82)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha("#d4fff8", 0.44)
                radius: 2
            }
        }
    }

    Item {
        id: cloneSourcePin
        objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneSourcePin" : "cloneSourcePin"
        property real sourceX: previewController.activeMaskCloneSourceX
        property real sourceY: previewController.activeMaskCloneSourceY
        property var sourcePoint: viewport.viewPointForImage(sourceX, sourceY)
        property string sourceStatus: previewController.activeMaskCloneSourceSet ? "Source set" : "Pick source"
        visible: workspace.page === "edit" &&
                 previewController.hasActiveMask &&
                 previewController.activeMaskToolRole === "retouch" &&
                 previewController.activeMaskRetouchMode === "clone" &&
                 previewController.activeMaskCloneSourceSet
        x: sourcePoint.x - width / 2
        y: sourcePoint.y - height / 2
        width: 34
        height: 34
        z: 23

        function moveSource(normalizedX, normalizedY) {
            previewController.setActiveMaskCloneSource(
                workspace.clamp(normalizedX, 0.0, 1.0),
                workspace.clamp(normalizedY, 0.0, 1.0))
        }

        Rectangle {
            anchors.centerIn: parent
            width: 24
            height: 24
            radius: 12
            color: theme.alpha("#7cb5ff", clonePinMouse.containsMouse || clonePinMouse.pressed ? 0.18 : 0.08)
            border.width: 2
            border.color: "#7cb5ff"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 2
            height: 32
            radius: 1
            color: "#f4f7fa"
        }

        Rectangle {
            anchors.centerIn: parent
            width: 32
            height: 2
            radius: 1
            color: "#f4f7fa"
        }

        Label {
            objectName: viewport.objectName.length > 0 ? viewport.objectName + "CloneSourceLabel" : "cloneSourceLabel"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.bottom
            anchors.topMargin: 4
            text: "Clone source"
            color: theme.textStrong
            font.family: theme.fontMono
            font.pixelSize: 10
            padding: 4
            background: Rectangle {
                color: theme.alpha(theme.ink950, 0.82)
                border.width: theme.chromeBorderWidth
                border.color: theme.alpha("#7cb5ff", 0.42)
                radius: 2
            }
        }

        MouseArea {
            id: clonePinMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.SizeAllCursor
            onPressed: function(mouse) {
                const mapped = mapToItem(viewport, mouse.x, mouse.y)
                const point = viewport.normalizedPointForViewport(mapped.x, mapped.y)
                if (point.valid)
                    cloneSourcePin.moveSource(point.x, point.y)
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                const mapped = mapToItem(viewport, mouse.x, mouse.y)
                const point = viewport.normalizedPointForViewport(mapped.x, mapped.y)
                if (point.valid)
                    cloneSourcePin.moveSource(point.x, point.y)
            }
        }
    }
}
