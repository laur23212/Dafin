import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 8
    color: "#1B2935"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#D9B48D" : "#AB8D6B"
    Rectangle { x: 3; y: 3; width: Math.max(0,parent.width-6); height: 1; color: "#99D9B48D" }
}
