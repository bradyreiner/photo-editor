import QtQuick

Image {
    id: root

    readonly property string effectContract: "timeline-difference-effect:v2|space:controller-linear-light-srgb|inputs:bounded-frame-files|alpha:opaque|gain:bounded|cache:latest-sixteen"
    fillMode: Image.PreserveAspectFit
    asynchronous: true
    cache: false
}
