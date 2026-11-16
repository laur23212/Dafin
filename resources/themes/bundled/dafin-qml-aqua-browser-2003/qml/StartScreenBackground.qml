import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#C6DCEB"
    border.width: 0
    border.color: "#5B9ECC"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#CCE0ED" }
        GradientStop { position: 1; color: "#C0DAEB" }
    }
}
