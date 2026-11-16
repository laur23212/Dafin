import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#0C0C0C"
    border.width: 1
    border.color: "#FFFFFF"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#0C0C0C" }
        GradientStop { position: 1; color: "#0C0C0C" }
    }
    Rectangle { x: 2; y: parent.height-4; height: 2; width: Math.max(0,parent.width-4); color: "#FFE900" }
}
