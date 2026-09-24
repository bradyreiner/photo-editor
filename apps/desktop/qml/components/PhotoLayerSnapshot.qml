import QtQml

QtObject {
    required property var controller

    // photoLayerCount and photoLayers share controlsChanged, including thumbnail
    // completion. The invokable returns a copy sequence: inspector field reads
    // must not repeatedly re-fetch the controller's entire reference sequence.
    readonly property var rows: controller && controller.photoLayerCount > 0
                                ? controller.photoLayerSnapshot() : []
}
