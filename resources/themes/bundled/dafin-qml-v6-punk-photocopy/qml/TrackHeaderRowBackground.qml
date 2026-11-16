import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#28292B"
    border.width: 0
    border.color: "#8B7671"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#303032" }
        GradientStop { position: 1; color: "#2D2D2F" }
    }
    Rectangle { x: 5; y: 4; width: Math.min(60,parent.width/4); height: 9; rotation: -4; color: "#70F4EEE7" }
    Item { x: Math.max(0,parent.width-74); y: 4; width: 69; height: 50; opacity: 0.5
        Repeater { model: 7; Rectangle { x: (index*11)%65; y: (index*7)%40; width: index%2 ? 19 : 10; height: 1; rotation: (index%3-1)*22; color: index%3 ? "#F4EEE7" : "#EF4953" } }
    }
    Rectangle { x: 0; y: Math.max(0,parent.height-3); width: parent.width; height: 2; color: "#88EF4953" }
}
