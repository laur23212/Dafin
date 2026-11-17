import QtQuick
import Drift

// Romanian Core 2: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool checked: !!(control && control.checked === true)
    readonly property bool focusState: !!(control && control.visualFocus === true)
    color: checked ? (Theme.darkMode ? "#B5293C" : "#B5293C") : (Theme.darkMode ? "#151E34" : "#FFF6D4")
    radius: 2
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#F7D756" : (Theme.darkMode ? "#D7C16B" : "#968336")
    Rectangle { anchors.centerIn: parent; width: parent.width*.42; height: width; rotation:45; color: checked ? "#FFF8DA" : "transparent" }
}
