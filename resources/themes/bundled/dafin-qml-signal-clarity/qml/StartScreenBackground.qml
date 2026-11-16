import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#000000"
    border.width: 1
    border.color: "#FFFFFF"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#000000" }
        GradientStop { position: 1; color: "#000000" }
    }
}
