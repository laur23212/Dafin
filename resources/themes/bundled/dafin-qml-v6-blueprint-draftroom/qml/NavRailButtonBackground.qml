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
    readonly property bool idleGhost: !root.activeState
    readonly property color baseFill: root.selectedState ? "#C0EEFF" : (root.pressedState ? "#082B4D" : (root.hoveredState ? "#3A749F" : "#235D8D"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#B5EEFF" : (root.selectedState ? "#B5EEFF" : "#8BC6E8")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#C9F0FF" : (root.pressedState ? "#082B4D" : "#356A96") }
            GradientStop { position: 1.0; color: root.selectedState ? "#C0EEFF" : root.pressedState ? "#235D8D" : "#1C507D" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#5690B5" }
    }
    Rectangle { x: 5; y: 3; width: Math.max(0,parent.width-10); height: 1; color: "#80DDF6FF" }
    Rectangle { x: 4; y: Math.max(0,parent.height-5); width: 6; height: 1; color: "#D9DFFAFF" }
    Rectangle { x: Math.max(0,parent.width-10); y: Math.max(0,parent.height-5); width: 6; height: 1; color: "#D9DFFAFF" }
}
