import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool checked: !!(control && control.checked === true)
    readonly property bool focusState: !!(control && control.visualFocus === true)
    color: checked ? (Theme.darkMode ? "#8E313B" : "#AE3946") : (Theme.darkMode ? "#1A282C" : "#D9CAB1")
    radius: 5
    border.width: focusState ? 2 : 1
    border.color: focusState ? "#EBD197" : (Theme.darkMode ? "#A88B62" : "#8F7350")
    Rectangle {
        x: root.checked ? Math.max(2,root.width-width-2) : 2
        y: 2; width: Math.max(6, root.height-4); height: Math.max(6,root.height-4)
        radius: 4
        color: "#F5E8C9"; border.width: 1; border.color: "#A58B5E"
        Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Easing.InOutQuad } }
        Rectangle { anchors.centerIn: parent; width: 7; height: 7; rotation: 45; border.width: 1; border.color: "#B52E38"; color: "transparent" }
    }
}
