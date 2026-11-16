import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#E5EAF0"
    border.width: 0
    border.color: "#9DAEBE"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#E8ECF2" }
        GradientStop { position: 1; color: "#E4E9EF" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: parent.radius; color: "transparent"; border.width: 2; border.color: "#B1FFFFFF" }
    Rectangle { x: 6; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-12); height: 2; color: "#39798FA9" }
}
