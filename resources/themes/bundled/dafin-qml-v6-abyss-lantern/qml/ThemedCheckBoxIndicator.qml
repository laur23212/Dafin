import QtQuick
import Drift

// Original visual-only Dafin skin. Trusted QML; no commands, I/O, or input handlers.
Rectangle {
    id: root
    property var control
    readonly property bool activeState: !!(control && control.checked === true)
    readonly property bool focusedState: !!(control && control.visualFocus === true)
    radius: 15
    color: root.activeState ? "#5CDFF2" : "#031221"
    border.width: root.focusedState ? 2 : 1
    border.color: root.focusedState ? "#68E5FA" : "#2B7088"
    Text { anchors.centerIn: parent; text: "✓"; visible: root.activeState; color: "#051A29"; font.pixelSize: Math.min(18,Math.max(12,parent.height - 4)); font.bold: true }
}
