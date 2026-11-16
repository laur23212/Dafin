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
    readonly property color baseFill: root.selectedState ? "#A51C50" : (root.pressedState ? "#EADBC9" : (root.hoveredState ? "#E9C0BB" : "#F0DFCC"))
    radius: 2
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#C21F64" : (root.selectedState ? "#C21F64" : "#536363")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#B23C68" : (root.pressedState ? "#EADBC9" : "#F3E6D8") }
            GradientStop { position: 1.0; color: root.selectedState ? "#A51C50" : root.pressedState ? "#F0DFCC" : "#EEDECB" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#F8F1E8" }
    }
    Rectangle { x: 2; y: 3; width: 6; height: Math.max(0,parent.height-6); color: "#78C21F64" }
    Rectangle { x: Math.max(0,parent.width-8); y: 4; width: 4; height: Math.max(0,parent.height-8); color: "#770E8289" }
}
