import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#AEE4F5FB"
    border.width: 0
    border.color: "#77B4C8"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#E9FFFFFF" }
        GradientStop { position: 1; color: "#CCDAEAF8" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: root.radius; color: "transparent"; border.width: 1; border.color: "#80FFFFFF" }
}
