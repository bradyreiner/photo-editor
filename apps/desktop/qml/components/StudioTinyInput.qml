import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts

Rectangle {
    id: field

    property alias text: input.text
    property string placeholder: ""
    property bool highlighted: false
    property color accent: theme.accentWarm400
    property int rightInset: 12
    readonly property bool userEditing: input.activeFocus
    readonly property string tinyInputContract: "tiny-input:v1|active-highlight:true|enter-accepted:true|right-inset:true|no-elide:true"

    function forceInputFocus() { input.forceActiveFocus() }
    function clearInputFocus() { input.focus = false }
    function selectAllText() { input.selectAll() }

    signal accepted()
    signal cancelled()
    signal editingFinished()

    Theme { id: theme }

    Layout.preferredHeight: 34
    color: field.highlighted ? theme.alpha(field.accent, 0.08) : theme.ink950
    border.width: input.activeFocus ? 2 : theme.chromeBorderWidth
    border.color: input.activeFocus
                  ? theme.alpha(theme.focusRing, 0.9)
                  : field.highlighted
                    ? theme.alpha(field.accent, 0.62)
                    : theme.alpha(theme.lineSoft, 0.58)
    radius: 3

    TextInput {
        id: input
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: field.rightInset
        verticalAlignment: TextInput.AlignVCenter
        color: theme.textMain
        selectionColor: theme.accentWarm400
        selectedTextColor: theme.ink950
        font.family: theme.fontUi
        font.pixelSize: 12
        clip: true
        Keys.onReturnPressed: field.accepted()
        Keys.onEnterPressed: field.accepted()
        Keys.onEscapePressed: function(event) {
            field.cancelled()
            event.accepted = true
        }
        onEditingFinished: field.editingFinished()
    }

    Label {
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.verticalCenter: parent.verticalCenter
        visible: input.text.length === 0
        text: field.placeholder
        color: theme.textMuted
        font.family: theme.fontUi
        font.pixelSize: 12
    }
}
