import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#0F4C81"
    border.width: 0
    border.color: "#8BC6E8"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#175285" }
        GradientStop { position: 1; color: "#134F83" }
    }
    Item { anchors.fill: parent; opacity: 0.19
        Repeater { model: 8; Rectangle { x: 0; y: index*(parent.height/8); width: parent.width; height: 1; color: "#B4ECFF" } }
        Repeater { model: 10; Rectangle { x: index*(parent.width/10); y: 0; height: parent.height; width: 1; color: "#B4ECFF" } }
    }
    Rectangle { x: 8; y: 8; width: 13; height: 1; color: "#C5F5FF" }
    Rectangle { x: 14; y: 2; width: 1; height: 13; color: "#C5F5FF" }
    Rectangle { x: Math.max(0,parent.width-24); y: Math.max(0,parent.height-15); width: 14; height: 1; color: "#C5F5FF" }
}
