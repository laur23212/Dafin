import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 12
    color: "#E8F4FC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#168FD1" : "#5B9ECC"
    Rectangle { x: 3; y: 3; height: Math.max(0,parent.height*.24); width: Math.max(0,parent.width-6); color: "#44FFFFFF"; radius: root.radius }
}
