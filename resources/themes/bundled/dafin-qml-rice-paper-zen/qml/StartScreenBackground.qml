import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F7F4EB"
    border.width: 0
    border.color: "#A8AC9C"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F8F5ED" }
        GradientStop { position: 1; color: "#F5F2E9" }
    }
    Rectangle { x: 10; y: 9; width: Math.min(46,parent.width-20); height: 1; color: "#68818D78" }
    Rectangle { x: Math.max(0,parent.width-27); y: 9; width: 12; height: 12; radius: 6;
                color: "transparent"; border.width: 1; border.color: "#6C6E775F" }
}
