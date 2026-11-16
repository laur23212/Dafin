import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 7
    color: "#FFF9EC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#B35E43" : "#CDB89A"
}
