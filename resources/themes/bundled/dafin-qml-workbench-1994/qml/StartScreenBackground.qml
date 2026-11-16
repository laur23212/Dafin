import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.selected === true || control.active === true || control.visualFocus === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#9EA7AC"
    border.width: 1
    border.color: "#657377"
    clip: true
    gradient: Gradient {
        GradientStop { position: 0; color: "#9EA7AC" }
        GradientStop { position: 1; color: "#9EA7AC" }
    }
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#F4F5F1" : "#EDEDEC" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#F4F5F1" : "#EDEDEC" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#EDEDEC" : "#657377" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#EDEDEC" : "#657377" }
}
