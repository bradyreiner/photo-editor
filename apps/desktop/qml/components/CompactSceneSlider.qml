import QtQuick
import QtQuick.Layouts

InspectorSlider {
    id: root
    property int decimals: 1
    property string suffix: ""
    signal valueEdited(real value)
    from: 0
    to: 1
    stepSize: 0.01
    forceCompact: true
    valueText: value.toFixed(decimals) + suffix
    onValueDragged: function(value) { root.valueEdited(value) }
    onResetRequested: root.valueEdited(defaultValue)
}
