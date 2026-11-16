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
    readonly property color baseFill: root.selectedState ? "#8AFFAF" : (root.pressedState ? "#020A05" : (root.hoveredState ? "#204C2D" : "#112A18"))
    radius: 0
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#70FF9B" : (root.selectedState ? "#70FF9B" : "#428A61")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#9AFFBA" : (root.pressedState ? "#020A05" : "#243B2A") }
            GradientStop { position: 1.0; color: root.selectedState ? "#8AFFAF" : root.pressedState ? "#112A18" : "#0D2213" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#327546" }
    }
    Repeater { model: 4
        Rectangle { x: 1; y: Math.max(0,(index+1)*parent.height/5); height: 1;
                    width: Math.max(0,parent.width-2); color: "#236FFF9B" }
    }
    Rectangle { visible: root.activeState; x: Math.max(0,parent.width/2); y: 1; width: Math.max(0,parent.width/3); height: 2; color: "#F6BD60" }
}
