import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#18191B"
    border.width: 1
    border.color: "#E8E8E8"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#202123" }
        GradientStop { position: 1; color: "#1B1C1E" }
    }
    Rectangle { x: 0; y: 0; width: Math.min(7,parent.width); height: parent.height; color: "#6EE60012" }
    Item { x: Math.max(0,parent.width-66); y: Math.max(0,parent.height-12); width: 60; height: 10; opacity: 0.50
        Repeater { model: 12; Rectangle { x: index*5; y: 0; width: index%3===0 ? 3 : 1; height: 9; color: "#F4F4F4" } }
    }
}
