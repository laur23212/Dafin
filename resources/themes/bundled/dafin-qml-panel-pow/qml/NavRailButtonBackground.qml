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
    readonly property color baseFill: root.selectedState ? "#D1253D" : (root.pressedState ? "#ECF1F4" : (root.hoveredState ? "#F9C353" : "#FFE157"))
    radius: 3
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#D7263D" : (root.selectedState ? "#D7263D" : "#173149")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#D74458" : (root.pressedState ? "#ECF1F4" : "#FFE87E") }
            GradientStop { position: 1.0; color: root.selectedState ? "#D1253D" : root.pressedState ? "#FFE157" : "#FAE57E" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#FFF2B3" }
    }
    // Overset ink line and Ben-Day mini-dots, original generic print-art style.
    Rectangle { x: 2; y: Math.max(0,parent.height-4); width: Math.max(0,parent.width-5); height: 2; color: "#173149" }
    Repeater { model: 4
        Rectangle { x: Math.max(0,parent.width-18)+(index%2)*6; y: 3+Math.floor(index/2)*6;
                    width: 2; height: 2; radius: 1; color: "#D7263D" }
    }
}
