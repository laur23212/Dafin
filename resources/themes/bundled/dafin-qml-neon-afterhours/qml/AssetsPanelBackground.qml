import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 6
    color: "#151B26"
    border.width: 1
    border.color: "#3C6776"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#1D232E" }
        GradientStop { position: 1; color: "#161D29" }
    }
}
