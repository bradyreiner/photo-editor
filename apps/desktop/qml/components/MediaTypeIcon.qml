import QtQuick

AppIcon {
    id: root

    property string mediaType: "photo"
    name: mediaType === "carousel" || mediaType === "boards" ? "carousel"
          : mediaType === "timeline" || mediaType === "compound" ? "timeline"
          : mediaType === "audio" ? "audio"
          : mediaType === "video" ? "video"
          : mediaType === "caption" ? "captions"
          : "image"
    Accessible.ignored: true
}
