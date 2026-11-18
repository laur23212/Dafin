import QtQuick
import Drift

// A duration you can type in milliseconds or seconds. The model always holds seconds
// (`seconds`, `edited(seconds)`); the small ms / s button beside the field only changes
// how the number is shown and typed. In ms the field counts whole milliseconds, in s it
// shows three decimals, so both reach the same 1 ms precision.
Row {
    id: root

    // Canonical value, in seconds. Bind it from the model; it is ignored while typing.
    property real seconds: 0
    property real minSeconds: 0
    property real maxSeconds: 10
    // Which unit the field starts in. The user can flip it with the button.
    property bool useSeconds: true
    // Scrub / arrow step, in seconds. Shown in the active unit.
    property real stepSeconds: 0.05

    signal edited(real seconds)

    spacing: 4

    function _toShown(s) {
        return root.useSeconds ? s : Math.round(s * 1000)
    }

    ThemedNumberField {
        id: field
        width: root.width - unitButton.width - root.spacing
        from: root._toShown(root.minSeconds)
        to: root._toShown(root.maxSeconds)
        step: root.useSeconds ? root.stepSeconds : Math.max(1, Math.round(root.stepSeconds * 1000))
        decimals: root.useSeconds ? 3 : 0
        unit: root.useSeconds ? "s" : "ms"
        Binding on value {
            when: !field.activeFocus
            value: root._toShown(root.seconds)
        }
        onEdited: v => root.edited(root.useSeconds ? v : v / 1000)
    }

    ThemedButton {
        id: unitButton
        width: 34
        height: field.height
        text: root.useSeconds ? "s" : "ms"
        variant: "secondary"
        tooltip: root.useSeconds ? qsTr("Showing seconds. Click for milliseconds.")
                                 : qsTr("Showing milliseconds. Click for seconds.")
        onClicked: root.useSeconds = !root.useSeconds
    }

    // The number changes with the unit, but a value of 0 does not, so redraw the text.
    onUseSecondsChanged: Qt.callLater(function () { field.text = field.format(field.value) })
}
