import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool checked: !!(control && control.checked === true)
    readonly property bool focusState: !!(control && control.visualFocus === true)
    color: checked ? (Theme.darkMode ? "#AD313C" : "#AD313C") : (Theme.darkMode ? "#1A2B2F" : "#FAEFD9")
    radius: 2
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#E1B76C" : (Theme.darkMode ? "#C6A77C" : "#866746")
    Rectangle { anchors.centerIn: parent; width: parent.width*.42; height: width; rotation:45; color: checked ? "#FFF5DA" : "transparent" }
}
