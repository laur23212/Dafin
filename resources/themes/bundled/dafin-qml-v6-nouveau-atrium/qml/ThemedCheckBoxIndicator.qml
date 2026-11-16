import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 12
    color: root.activeState ? "#E7C57A" : "#16342D"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#E7C57A" : "#B4A474"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#1B302C"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
