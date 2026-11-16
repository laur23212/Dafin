import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool pressedState: !!(control && control.down === true)
    readonly property bool focusedState: !!(control && (control.visualFocus === true || control.activeFocus === true))
    readonly property bool selectedState: !!(control && (control.selected === true || control.active === true || control.checked === true || control.variant === "primary"))
    readonly property bool hoveredState: !!(control && control.hovered === true)
    readonly property bool activeState: selectedState || hoveredState || focusedState
    readonly property bool idleGhost: false
    readonly property color baseFill: root.selectedState ? "#F3B84D" : (root.pressedState ? "#20272D" : (root.hoveredState ? "#615E52" : "#454D53"))
    radius: 2
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 2)
    border.color: root.focusedState ? "#F3B84D" : (root.selectedState ? "#F3B84D" : "#79848A")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 2
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#F5C266" : (root.pressedState ? "#20272D" : "#545B61") }
            GradientStop { position: 1.0; color: root.selectedState ? "#F3B84D" : root.pressedState ? "#454D53" : "#3C444A" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#20272D" : "#827251" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#20272D" : "#827251" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#827251" : "#79848A" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#827251" : "#79848A" }
    }
    Repeater { model: 2; Rectangle { width: 3; height: 3; radius: 2; color: "#C9A26A"; x: index===0 ? 4 : Math.max(4,parent.width-7); y: Math.max(2,parent.height/2-1) } }
}
