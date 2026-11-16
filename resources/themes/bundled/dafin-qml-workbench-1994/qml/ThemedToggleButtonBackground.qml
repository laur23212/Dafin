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
    readonly property color baseFill: root.selectedState ? "#176C79" : (root.pressedState ? "#F4F5F1" : (root.hoveredState ? "#B6C7C5" : "#D6D8D4"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 2)
    border.color: root.focusedState ? "#116E75" : (root.selectedState ? "#116E75" : "#657377")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 2
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#37818C" : (root.pressedState ? "#F4F5F1" : "#DFE1DE") }
            GradientStop { position: 1.0; color: root.selectedState ? "#176C79" : root.pressedState ? "#D6D8D4" : "#DEDFDB" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#F4F5F1" : "#EDEDEC" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#F4F5F1" : "#EDEDEC" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.pressedState ? "#EDEDEC" : "#657377" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.pressedState ? "#EDEDEC" : "#657377" }
    }
}
