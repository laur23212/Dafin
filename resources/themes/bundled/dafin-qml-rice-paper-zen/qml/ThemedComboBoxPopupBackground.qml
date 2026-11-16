import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 5
    color: "#FFFDF6"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#536E57" : "#A8AC9C"
    Rectangle { x: 10; y: 9; width: Math.min(46,parent.width-20); height: 1; color: "#68818D78" }
    Rectangle { x: Math.max(0,parent.width-27); y: 9; width: 12; height: 12; radius: 6;
                color: "transparent"; border.width: 1; border.color: "#6C6E775F" }
}
