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
    readonly property color baseFill: root.selectedState ? "#E7C57A" : (root.pressedState ? "#16342D" : (root.hoveredState ? "#58745F" : "#3D655A"))
    radius: 12
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#E7C57A" : (root.selectedState ? "#E7C57A" : "#B4A474")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#EACD8D" : (root.pressedState ? "#16342D" : "#4D7167") }
            GradientStop { position: 1.0; color: root.selectedState ? "#E7C57A" : root.pressedState ? "#3D655A" : "#33594F" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#788765" }
    }
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 10; color: "transparent"; border.color: "#88E7C57A"; border.width: 1 }
    Rectangle { x: 7; y: 3; width: 8; height: 3; radius: 3; rotation: -20; color: "#C7CF9CB5" }
}
