import QtQuick

Item {
    id: root

    property Item sourceItem: null
    property var effects: []
    property string frameMode: "fit"
    property real sourceAspectRatio: 16 / 9
    readonly property real itemAspectRatio: width / Math.max(1, height)
    readonly property real boundedSourceAspectRatio: Math.max(0.01, Number(sourceAspectRatio || 16 / 9))
    readonly property real contentWidth: frameMode === "stretch"
                                         ? width
                                         : frameMode === "fill"
                                           ? (boundedSourceAspectRatio > itemAspectRatio
                                              ? height * boundedSourceAspectRatio : width)
                                           : (boundedSourceAspectRatio > itemAspectRatio
                                              ? width : height * boundedSourceAspectRatio)
    readonly property real contentHeight: frameMode === "stretch"
                                          ? height : contentWidth / boundedSourceAspectRatio
    readonly property real contentLeft: (width - contentWidth) / 2
    readonly property real contentTop: (height - contentHeight) / 2
    readonly property vector4d pixelSize: Qt.vector4d(
        Math.max(1, contentWidth), Math.max(1, contentHeight), 0, 0)
    readonly property vector4d contentRect: Qt.vector4d(
        contentLeft / Math.max(1, width), contentTop / Math.max(1, height),
        contentWidth / Math.max(1, width), contentHeight / Math.max(1, height))

    function effectCode(name) {
        switch (String(name || "")) {
        case "Exposure": return 1
        case "Contrast": return 2
        case "Saturation": return 3
        case "Black & White": return 4
        case "Fade": return 5
        case "Punch": return 6
        default: return 0
        }
    }

    function effectAt(index) {
        return effects && index < effects.length ? effects[index] : ({})
    }

    function effectVector(index) {
        const effect = effectAt(index)
        const mask = effect.mask || ({})
        return Qt.vector4d(effectCode(effect.name), Number(effect.amount || 0),
                           effectCode(effect.name) === 0 ? 0 : 1,
                           mask.inverted === true ? 1 : 0)
    }

    function geometryVector(index) {
        const mask = effectAt(index).mask || ({})
        return Qt.vector4d(Number(mask.centerX !== undefined ? mask.centerX : 0.5),
                           Number(mask.centerY !== undefined ? mask.centerY : 0.5),
                           Number(mask.width || 0.65), Number(mask.height || 0.65))
    }

    function maskVector(index) {
        const effect = effectAt(index)
        const mask = effect.mask || ({})
        return Qt.vector4d(Number(mask.feather || 0),
                           String(mask.shape || "ellipse") === "rectangle" ? 1 : 0,
                           0, 0)
    }

    property vector4d effect0: effectVector(0)
    property vector4d geometry0: geometryVector(0)
    property vector4d mask0: maskVector(0)
    property vector4d effect1: effectVector(1)
    property vector4d geometry1: geometryVector(1)
    property vector4d mask1: maskVector(1)
    property vector4d effect2: effectVector(2)
    property vector4d geometry2: geometryVector(2)
    property vector4d mask2: maskVector(2)
    property vector4d effect3: effectVector(3)
    property vector4d geometry3: geometryVector(3)
    property vector4d mask3: maskVector(3)
    property vector4d effect4: effectVector(4)
    property vector4d geometry4: geometryVector(4)
    property vector4d mask4: maskVector(4)
    property vector4d effect5: effectVector(5)
    property vector4d geometry5: geometryVector(5)
    property vector4d mask5: maskVector(5)

    ShaderEffectSource {
        id: effectTexture
        anchors.fill: parent
        sourceItem: root.sourceItem
        live: true
        recursive: true
        hideSource: root.visible
        visible: root.visible
    }

    ShaderEffect {
        anchors.fill: parent
        visible: root.visible
        property var source: effectTexture
        property vector4d pixelSize: root.pixelSize
        property vector4d contentRect: root.contentRect
        vertexShader: "qrc:/photo-editor/shaders/timeline_effect_mask.vert.qsb"
        fragmentShader: "qrc:/photo-editor/shaders/timeline_effect_mask.frag.qsb"
        property vector4d effect0: root.effect0
        property vector4d geometry0: root.geometry0
        property vector4d mask0: root.mask0
        property vector4d effect1: root.effect1
        property vector4d geometry1: root.geometry1
        property vector4d mask1: root.mask1
        property vector4d effect2: root.effect2
        property vector4d geometry2: root.geometry2
        property vector4d mask2: root.mask2
        property vector4d effect3: root.effect3
        property vector4d geometry3: root.geometry3
        property vector4d mask3: root.mask3
        property vector4d effect4: root.effect4
        property vector4d geometry4: root.geometry4
        property vector4d mask4: root.mask4
        property vector4d effect5: root.effect5
        property vector4d geometry5: root.geometry5
        property vector4d mask5: root.mask5
    }
}
