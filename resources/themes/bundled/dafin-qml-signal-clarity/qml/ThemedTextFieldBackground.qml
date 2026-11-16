import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 2
    color: "#0C0C0C"
    border.width: root.activeState ? 4 : 2
    border.color: root.activeState ? "#FFE900" : "#FFFFFF"
}
