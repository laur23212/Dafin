import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 8
    color: root.activeState ? "#D6B18A" : "#0B1820"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#D9B48D" : "#AB8D6B"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#1A2830"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
