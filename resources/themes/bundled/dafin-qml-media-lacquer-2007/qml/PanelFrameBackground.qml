import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 10
    color: "#1C242B"
    border.width: 1
    border.color: "#4D6C7B"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#242C32" }
        GradientStop { position: 1; color: "#202A31" }
    }
}
