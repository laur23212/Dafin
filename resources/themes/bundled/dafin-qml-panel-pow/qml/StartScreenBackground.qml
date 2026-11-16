import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#F6F6F3"
    border.width: 0
    border.color: "#173149"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#F7F7F4" }
        GradientStop { position: 1; color: "#F8F2D7" }
    }
    Rectangle { x: 0; y: Math.max(0,parent.height-3); width: parent.width; height: 3; color: "#173149" }
    Item { x: Math.max(0,parent.width-68); y: 4; width: 60; height: 30; opacity: 0.46
        Repeater { model: 18
            Rectangle { x: (index%6)*9; y: Math.floor(index/6)*9; width: 3; height: 3; radius: 2; color: "#D7263D" }
        }
    }
}
