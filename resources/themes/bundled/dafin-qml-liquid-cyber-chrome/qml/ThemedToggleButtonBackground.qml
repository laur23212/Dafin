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
    readonly property color baseFill: root.selectedState ? "#6A439C" : (root.pressedState ? "#E2EBF5" : (root.hoveredState ? "#CCBFE1" : "#D1D7E5"))
    radius: 18
    color: root.idleGhost ? "transparent" : baseFill
    border.width: root.idleGhost ? 0 : (root.focusedState ? 2 : 1)
    border.color: root.focusedState ? "#B041C9" : (root.selectedState ? "#B041C9" : "#8395B6")
    opacity: control && control.enabled === false ? 0.55 : 1.0
    clip: true
    Rectangle {
        anchors.fill: parent; anchors.margins: 1
        radius: root.radius
        color: "transparent"
        visible: !root.idleGhost
        gradient: Gradient {
            GradientStop { position: 0.0; color: root.selectedState ? "#7F5DAA" : (root.pressedState ? "#E2EBF5" : "#DCE0EB") }
            GradientStop { position: 1.0; color: root.selectedState ? "#6A439C" : root.pressedState ? "#D1D7E5" : "#D5DCE9" }
        }
    }
    Item { anchors.fill: parent; visible: !root.idleGhost
    Rectangle { x: 3; y: 2; width: Math.max(0,parent.width-6); height: 2; color: "#EAEDF3" }
    }
    // Liquid silver band, iridescent side-rail and lens flare; no external texture.
    Rectangle { x: 4; y: 2; width: Math.max(0,parent.width-8); height: Math.max(0,parent.height*.42); radius: parent.radius;
        gradient: Gradient { GradientStop { position: 0; color: "#AAFFFFFF" }
                             GradientStop { position: 1; color: "#11FFFFFF" } } }
    Rectangle { x: 3; y: Math.max(0,parent.height-5); width: Math.max(0,parent.width-6); height: 2; radius: 1; color: "#669C70FF" }
    Rectangle { x: 2; y: 5; width: 2; height: Math.max(0,parent.height-10); color: "#77B041C9" }
    Rectangle { x: Math.max(0,parent.width-4); y: 5; width: 2; height: Math.max(0,parent.height-10); color: "#661AAAC6" }
}
