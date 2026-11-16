import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 3
    color: "#FFFFFF"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#D7263D" : "#173149"
    Rectangle { x: 2; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-4); height: 2; color: "#173149" }
}
