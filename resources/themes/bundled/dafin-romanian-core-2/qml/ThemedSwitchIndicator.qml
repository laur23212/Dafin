import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool checked: !!(control && control.checked === true)
    readonly property bool focusState: !!(control && control.visualFocus === true)
    color: checked ? (Theme.darkMode ? "#9C2333" : "#BC2B3E") : (Theme.darkMode ? "#151D31" : "#E3D7A7")
    radius: 5
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#FAE388" : (Theme.darkMode ? "#BBA54F" : "#A08C3F")
    Rectangle {
        x: root.checked ? Math.max(2,root.width-width-2) : 2
        y: 2; width: Math.max(6, root.height-4); height: Math.max(6,root.height-4)
        radius: 4
        color: "#FDF1C1"; border.width: 1; border.color: "#B8A24B"
        Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.InOutQuad } }
        Rectangle { anchors.centerIn: parent; width: 7; height: 7; rotation: 45; border.width: 1; border.color: "#B92A3D"; color: "transparent" }
    }
}
