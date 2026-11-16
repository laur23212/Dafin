import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 3
    color: root.activeState ? "#22DCEB" : "#101823"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#22DCEB" : "#3C6776"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#071418"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
