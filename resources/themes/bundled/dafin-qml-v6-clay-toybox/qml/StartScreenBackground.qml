import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F4E8F8"
    border.width: 0
    border.color: "#CBA9DF"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F5EBF9" }
        GradientStop { position: 1; color: "#F2E4F7" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 19; color: "transparent"; border.color: "#9FFFFFFF"; border.width: 3 }
    Rectangle { x: 10; y: Math.max(0,parent.height-7); width: Math.max(0,parent.width-20); height: 3; radius: 2; color: "#44987FB8" }
}
