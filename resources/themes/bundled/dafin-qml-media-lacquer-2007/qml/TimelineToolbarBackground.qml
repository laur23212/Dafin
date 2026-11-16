import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#1C242B"
    border.width: 0
    border.color: "#4D6C7B"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#242C32" }
        GradientStop { position: 1; color: "#202A31" }
    }
    Rectangle { x: 4; y: 2; width: Math.max(0,parent.width-8); height: 2; color: "#A4E8EC"; opacity: 0.35 }
    Rectangle { x: 4; y: parent.height-4; width: Math.max(0,parent.width-8); height: 1; color: "#335466" }
}
