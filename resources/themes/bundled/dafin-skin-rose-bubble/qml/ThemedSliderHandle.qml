import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    radius: 10
    color: "#ffe0f5"
    border.color: control && control.visualFocus ? "#c53084" : "#e59bbf"
    border.width: 1
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width-4); height: 2; radius: 1; color: "#ffffff" }
}
