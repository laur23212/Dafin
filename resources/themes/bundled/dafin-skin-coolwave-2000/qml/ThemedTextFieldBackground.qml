import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    readonly property bool isFocus: !!(control && (control.activeFocus || control.visualFocus || control.down))
    radius: 0
    color: "#FFFFFF"
    border.width: isFocus ? 2 : 1
    border.color: isFocus ? "#147b92" : "#536270"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width - 4); height: 1; color: "#6e7581"; opacity: 1 }
}
