import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 7
    color: "#abf6f1"
    border.color: control && control.visualFocus ? "#0787ac" : "#51b6d0"
    border.width: 1
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; radius: 1; color: "#ffffff" }
}
