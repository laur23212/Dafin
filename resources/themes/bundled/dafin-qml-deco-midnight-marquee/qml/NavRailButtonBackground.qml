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
    readonly property color baseFill: root.selectedState ? "#D4AF37" : (root.pressedState ? "#101929" : (root.hoveredState ? "#414648" : "#25324B"))
    radius: 1
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#D4AF37" : (root.selectedState ? "#D4AF37" : "#A68B4E")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#DABA53" : (root.pressedState ? "#101929" : "#364259") }
            GradientStop { position: 1.0; color: root.selectedState ? "#D4AF37" : root.pressedState ? "#25324B" : "#202C42" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#625E44" }
    }
    // Double-gold pinstripe and stepped Deco corner inlays.
    Rectangle { anchors.fill: parent; anchors.margins: 3; radius: 0; color: "transparent"; border.width: 1; border.color: "#8DD4AF37" }
    Rectangle { x: 3; y: 3; width: 7; height: 2; color: "#D4AF37" }
    Rectangle { x: 3; y: 3; width: 2; height: 7; color: "#D4AF37" }
    Rectangle { x: Math.max(0,parent.width-10); y: Math.max(0,parent.height-5); width: 7; height: 2; color: "#D4AF37" }
    Rectangle { x: Math.max(0,parent.width-5); y: Math.max(0,parent.height-10); width: 2; height: 7; color: "#D4AF37" }
}
