import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 22
    color: "#F2F6FB"
    border.width: 1
    border.color: "#8395B6"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F3F7FB" }
        GradientStop { position: 1; color: "#ECF0F7" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 2; radius: parent.radius; color: "transparent"; border.width: 1; border.color: "#91FFFFFF" }
    Rectangle { x: 4; y: 2; width: Math.max(0,parent.width-8); height: Math.min(14,parent.height/4); radius: 7; color: "#45FFFFFF" }
    Rectangle { x: 12; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-24); height: 2; color: "#309B58DB" }
}
