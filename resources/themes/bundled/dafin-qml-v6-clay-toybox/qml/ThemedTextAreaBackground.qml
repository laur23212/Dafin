import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 21
    color: "#FFF8FC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#9B5CBF" : "#CBA9DF"
    Rectangle { x: 4; y: 4; width: Math.max(0,parent.width-8); height: 2; radius: 1; color: "#719E82B3" }
}
