import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool picked: !!(control && (control.selected === true || control.checked === true || control.active === true))
    readonly property bool focusState: !!(control && control.visualFocus === true)
    readonly property bool press: !!(control && control.down === true)
    radius: 5
    color: picked ? (Theme.darkMode ? "#8F2F39" : "#A53642") : press ? (Theme.darkMode ? "#24363A" : "#D2BCA2") : (Theme.darkMode ? "#3A4C4C" : "#E8D9BE")
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#F2D292" : (Theme.darkMode ? "#A9926E" : "#9F805C")
    Rectangle { x: 5; y: 2; width: Math.max(0,parent.width-10); height: 1; color: Theme.darkMode ? "#E6D2B5" : "#FFF9ED"; opacity: .45 }
}
