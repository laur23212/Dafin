import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 7
    color: root.activeState ? "#73DCE3" : "#11191F"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#71DCE7" : "#4D6C7B"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#061B23"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
