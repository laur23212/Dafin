import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    readonly property bool isFocus: !!(control && (control.activeFocus || control.visualFocus || control.down))
    radius: 3
    color: "#1D160F"
    border.width: isFocus ? 2 : 1
    border.color: isFocus ? "#d1a05b" : "#966d42"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width - 4); height: 1; color: "#0d0907"; opacity: 0.35 }
}
