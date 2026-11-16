import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 12
    color: "#FFF9EC"
    border.width: 1
    border.color: "#CDB89A"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#FFFAEE" }
        GradientStop { position: 1; color: "#FDF6E8" }
    }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: "#D4A886"; opacity: .7 }
}
