import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    readonly property bool primaryState: !!(control && control.variant === "primary")
    readonly property color primaryFill: "#c53084"
    readonly property color pressedFill: "#d17aa8"
    readonly property color defaultFill: "#f9c8e3"
    radius: 18
    color: control && control.variant === "ghost" && !control.hovered && !control.down ? "transparent" : (root.primaryState ? root.primaryFill : (root.downState ? root.pressedFill : root.defaultFill))
    border.width: 1
    border.color: control && control.visualFocus ? "#c53084" : "#e59bbf"
    opacity: control && !control.enabled ? 0.45 : 1
    gradient: Gradient {
            GradientStop { position: 0; color: root.primaryState ? "#c53084" : (root.downState ? "#d17aa8" : "#FFF8FC") }
            GradientStop { position: 1; color: root.primaryState ? "#c53084" : (root.downState ? "#f9c8e3" : "#FCD4EC") }
        }
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.max(2,parent.height * 0.39); radius: parent.radius; color: "#77FFFFFF"; opacity: root.downState ? 0.15 : 0.83 }
}
