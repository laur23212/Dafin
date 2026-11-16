import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 5
    color: "#2B3339"
    border.width: 2
    border.color: "#79848A"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#323A40" }
        GradientStop { position: 1; color: "#30383E" }
    }
    Repeater { model: 4
        Rectangle {
            width: 4; height: 4; radius: 2; color: "#E6BB78"; border.color: "#222222"; border.width: 1
            x: index % 2 ? Math.max(3,parent.width-9) : 5
            y: index < 2 ? 5 : Math.max(3,parent.height-9)
        }
    }
}
