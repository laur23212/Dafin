import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 21
    color: root.activeState ? "#774A9B" : "#E1D5E9"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#9B5CBF" : "#CBA9DF"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#FFFFFF"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
