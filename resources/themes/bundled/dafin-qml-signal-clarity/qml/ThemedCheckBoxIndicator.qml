import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 2
    color: root.activeState ? "#FFE900" : "#000000"
    border.width: root.focusedState ? 4 : 1
    border.color: root.focusedState ? "#FFE900" : "#FFFFFF"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#000000"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
