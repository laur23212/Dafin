import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 0
    color: root.activeState ? "#8BFFAA" : "#010904"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#7EFFAA" : "#2D7250"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#061208"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
