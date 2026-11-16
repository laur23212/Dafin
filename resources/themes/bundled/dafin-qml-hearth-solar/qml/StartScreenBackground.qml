import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#FDF6E3"
    border.width: 0
    border.color: "#CDB89A"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#FDF7E6" }
        GradientStop { position: 1; color: "#FBF4E0" }
    }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: "#D4A886"; opacity: .7 }
}
