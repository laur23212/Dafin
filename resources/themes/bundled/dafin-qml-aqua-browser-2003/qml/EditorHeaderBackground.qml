import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#E8F4FC"
    border.width: 0
    border.color: "#5B9ECC"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#EBF5FC" }
        GradientStop { position: 1; color: "#DCEEF9" }
    }
}
