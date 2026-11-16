import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#ECEFF4"
    border.width: 0
    border.color: "#BAC7D4"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#ECEFF4" }
        GradientStop { position: 1; color: "#ECEFF4" }
    }
    Rectangle { x: 8; y: Math.max(0,parent.height-1); height: 1; width: Math.max(0,parent.width-16); color: "#CBD7E2"; opacity: .8 }
}
