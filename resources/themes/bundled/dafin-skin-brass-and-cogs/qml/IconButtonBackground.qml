import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool downState: !!(control && control.down === true)
    radius: 4
    color: root.downState ? "#0d0907" : (control && (control.active || control.hovered) ? "#513826" : "#00000000")
    border.width: 2
    border.color: control && control.visualFocus ? "#d1a05b" : "#966d42"
    opacity: control && !control.enabled ? 0.45 : 1
    
    Rectangle { x: 1; y: 1; width: Math.max(0,parent.width-2); height: 1; color: root.downState ? "#0d0907" : "#eed4a7" }
    Rectangle { x: 1; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.downState ? "#0d0907" : "#eed4a7" }
    Rectangle { x: 1; y: parent.height-2; width: Math.max(0,parent.width-2); height: 1; color: root.downState ? "#eed4a7" : "#0d0907" }
    Rectangle { x: parent.width-2; y: 1; width: 1; height: Math.max(0,parent.height-2); color: root.downState ? "#eed4a7" : "#0d0907" }
    Repeater { model: 2; Rectangle { width: 3; height: 3; radius: 1.5; color: "#EAC48E"; y: parent.height / 2-1; x: index === 0 ? 3 : parent.width - 6 } }
}
