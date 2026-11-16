import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 1
    color: root.activeState ? "#EF68CD" : "#100C2B"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#FA67D5" : "#7656BA"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#261331"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
