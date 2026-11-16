import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F3E9D9"
    border.width: 0
    border.color: "#536363"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F4EBDD" }
        GradientStop { position: 1; color: "#F2E7D7" }
    }
    Item { x: Math.max(0,parent.width-68); y: 4; width: 62; height: 40; opacity: 0.35
        Repeater { model: 24; Rectangle { x: (index%6)*10; y: Math.floor(index/6)*10; width: 3; height: 3; radius: 2; color: index%3 ? "#C21F64" : "#0E8289" } }
    }
    Rectangle { x: 2; y: Math.max(0,parent.height-3); width: Math.max(0,parent.width-4); height: 2; color: "#66C21F64" }
}
