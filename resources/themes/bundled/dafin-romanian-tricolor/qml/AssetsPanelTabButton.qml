import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property bool active: !!(control && (control.selected === true || control.checked === true || control.active === true))
    color: active ? (Theme.darkMode ? "#902130" : "#C72D42") : "transparent"
    radius: 5
    border.width: active ? 1 : 0
    border.color: Theme.darkMode ? "#E4CD70" : "#9D8833"
    Rectangle { anchors.bottom:parent.bottom; height:2; width:parent.width; color:Theme.darkMode ? "#F8DA63" : "#AA2638"; visible:active }
}
