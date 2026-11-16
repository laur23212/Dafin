import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 6
    color: root.activeState ? "#255E7E" : "#F0F3F7"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#386B8B" : "#BAC7D4"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#FFFFFF"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
