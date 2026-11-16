import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 0
    color: root.activeState ? "#C0EEFF" : "#082B4D"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#B5EEFF" : "#8BC6E8"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#0A2944"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
