import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#050C08"
    border.width: 0
    border.color: "#2D7250"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#050C08" }
        GradientStop { position: 1; color: "#050C08" }
    }
    Repeater { model: 6
        Rectangle { width: parent.width; height: 1; y: index * Math.max(1,parent.height/6); color: "#15442D"; opacity: 0.35 }
    }
}
