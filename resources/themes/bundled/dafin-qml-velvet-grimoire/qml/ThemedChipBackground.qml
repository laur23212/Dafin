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
    readonly property color baseFill: root.selectedState ? "#C999BC" : (root.pressedState ? "#1F1721" : (root.hoveredState ? "#624B61" : "#4B394D"))
    radius: 6
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#DAA8CA" : (root.selectedState ? "#DAA8CA" : "#9A748D")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#D1A7C5" : (root.pressedState ? "#1F1721" : "#59495B") }
            GradientStop { position: 1.0; color: root.selectedState ? "#C999BC" : root.pressedState ? "#4B394D" : "#403042" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#7D6079" }
    }
    Rectangle { x: 4; y: 4; width: 5; height: 5; rotation: 45; color: "#B5A8E2"; opacity: 0.7 }
    Rectangle { x: Math.max(0,parent.width-12); y: Math.max(0,parent.height-11); width: 4; height: 4; rotation: 45; color: "#DAA8CA"; opacity: 0.7 }
}
