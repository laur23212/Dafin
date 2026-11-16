import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#101418"
    border.width: 0
    border.color: "#4D6C7B"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#181C20" }
        GradientStop { position: 1; color: "#161C21" }
    }
}
