import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 0
    color: "#18191B"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#E60012" : "#E8E8E8"
    Rectangle { x: 0; y: 0; width: 3; height: parent.height; color: "#E60012" }
}
