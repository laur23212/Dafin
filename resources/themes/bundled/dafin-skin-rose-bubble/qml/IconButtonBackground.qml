import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    radius: 18
    color: root.downState ? "#d17aa8" : (control && (control.active || control.hovered) ? "#f9c8e3" : "#00000000")
    border.width: 1
    border.color: control && control.visualFocus ? "#c53084" : "#e59bbf"
    opacity: control && !control.enabled ? 0.45 : 1
    
    Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; anchors.margins: 2; height: Math.max(2,parent.height * 0.39); radius: parent.radius; color: "#77FFFFFF"; opacity: root.downState ? 0.15 : 0.83 }
}
