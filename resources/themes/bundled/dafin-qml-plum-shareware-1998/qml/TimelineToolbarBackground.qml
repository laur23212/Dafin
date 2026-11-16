import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#EAE2F2"
    border.width: 1
    border.color: "#8F72A8"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#ECE5F3" }
        GradientStop { position: 1; color: "#E6DBEE" }
    }
    Row { x: 6; y: 5; spacing: 4
        Repeater { model: 3; Rectangle { width: 3; height: 3; radius: 0; color: "#9B6BBA"; opacity: 0.66 } }
    }
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#FBF5FF" : "#EAE0F0" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#FBF5FF" : "#EAE0F0" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#EAE0F0" : "#8F72A8" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#EAE0F0" : "#8F72A8" }
}
