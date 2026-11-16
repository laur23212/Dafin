import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 2
    color: "#FFF6E9"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#C21F64" : "#536363"
    Item { x: Math.max(0,parent.width-68); y: 4; width: 62; height: 40; opacity: 0.35
        Repeater { model: 24; Rectangle { x: (index%6)*10; y: Math.floor(index/6)*10; width: 3; height: 3; radius: 2; color: index%3 ? "#C21F64" : "#0E8289" } }
    }
    Rectangle { x: 2; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-4); height: 2; color: "#66C21F64" }
}
