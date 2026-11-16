import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F2F2F2"
    border.width: 0
    border.color: "#A0A0A0"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F2F2F2" }
        GradientStop { position: 1; color: "#F2F2F2" }
    }
}
