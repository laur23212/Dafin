import QtQuick
import Drift

// Dafin original passive visual skin: no script execution or input handlers.
Rectangle {
    property var control
    readonly property bool isFocus: !!(control && (control.activeFocus || control.visualFocus || control.down))
    radius: 13
    color: "#fff3fa"
    border.width: isFocus ? 2 : 1
    border.color: isFocus ? "#c53084" : "#e59bbf"
    Rectangle { x: 2; y: 2; width: Math.max(0,parent.width - 4); height: 1; color: "#d17aa8"; opacity: 0.35 }
}
