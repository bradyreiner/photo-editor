import QtQuick
import QtQuick.Controls.Basic

Label {
    Theme { id: theme }
    readonly property string bodyTextStyleContract: "body-text:v1|shared-type-scale|fixed-size|minimum:micro"
    readonly property int bodyMinimumPixelSize: theme.typeMicro
    font.family: theme.fontUi
    font.pixelSize: theme.typeBody
    color: theme.textStrong
    textFormat: Text.PlainText
    fontSizeMode: Text.FixedSize
    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
    elide: Text.ElideNone
}
