import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#1A1537"
    border.width: 1
    border.color: "#7656BA"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#221D3E" }
        GradientStop { position: 1; color: "#1C173B" }
    }
    Rectangle { x: 2; y: parent.height-3; width: Math.max(0,parent.width-4); height: 1; color: "#51DDF0"; opacity: 0.7 }
    Rectangle { x: 2; y: 2; height: Math.max(0,parent.height-4); width: 2; color: "#FA67D5"; opacity: 0.65 }
}
