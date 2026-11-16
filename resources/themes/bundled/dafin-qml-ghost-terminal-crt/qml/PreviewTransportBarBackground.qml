import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#07170C"
    border.width: 0
    border.color: "#428A61"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#101F15" }
        GradientStop { position: 1; color: "#091A0E" }
    }
    Repeater { model: 8
        Rectangle { x: 0; y: (index+.5)*parent.height/8; height: 1; width: parent.width; color: "#136DFF9B" }
    }
    Rectangle { x: 6; y: 1; width: Math.max(0,parent.width-12); height: 1; color: "#6A70FF9B" }
}
