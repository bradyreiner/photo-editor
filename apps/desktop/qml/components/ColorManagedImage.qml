import QtQuick

Image {
    id: image
    property url imageSource
    property var displayController: typeof editorPreview !== "undefined" ? editorPreview : null
    source: imageSource

    Binding on colorSpace {
        when: !!image.displayController && image.displayController.thumbnailDisplayColorSpace !== undefined
        value: when ? image.displayController.thumbnailDisplayColorSpace : undefined
    }

    onColorSpaceChanged: {
        // Qt updates the reader's target profile without reloading its pixels.
        // Restore a binding so later asset/revision changes still update normally.
        source = ""
        source = Qt.binding(function() { return image.imageSource })
    }
}
