import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 1
    color: root.activeState ? "#D4AF37" : "#101929"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#D4AF37" : "#A68B4E"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#111B2C"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
