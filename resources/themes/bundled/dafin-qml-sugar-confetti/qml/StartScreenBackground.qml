import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F6EDF9"
    border.width: 0
    border.color: "#D5A8D7"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F7EFFA" }
        GradientStop { position: 1; color: "#F4E7F9" }
    }
    Rectangle { x: Math.max(0,parent.width-25); y: 7; width: 9; height: 9; radius: 5; color: "#FFE5C4"; opacity: .85 }
}
