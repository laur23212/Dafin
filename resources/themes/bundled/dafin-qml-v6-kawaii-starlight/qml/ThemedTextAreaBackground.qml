import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && (control.activeFocus === true || control.visualFocus === true || control.down === true))
    readonly property bool pressedState: !!(control && control.down === true)
    radius: 20
    color: "#FFF8FC"
    border.width: root.activeState ? 2 : 1
    border.color: root.activeState ? "#BD438B" : "#D79BBC"
    Text { x: Math.max(0,parent.width-12); y: 2; text: "♥"; font.pixelSize: 9; color: "#BD438B" }
}
