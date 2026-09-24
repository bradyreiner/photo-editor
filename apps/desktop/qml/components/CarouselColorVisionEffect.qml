import QtQuick

ShaderEffect {
    id: root

    property var source
    property int simulationMode: 0
    readonly property string effectContract: "carousel-color-vision-effect:v1|modes:protanopia-deuteranopia-tritanopia-grayscale|transfer:srgb-linear-srgb|alpha:premultiplied-safe|preview-only:true"

    vertexShader: "qrc:/photo-editor/shaders/carousel_color_vision.vert.qsb"
    fragmentShader: "qrc:/photo-editor/shaders/carousel_color_vision.frag.qsb"
}
