import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 2
    color: root.activeState ? "#F3B84D" : "#20272D"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#F3B84D" : "#79848A"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#22201A"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
