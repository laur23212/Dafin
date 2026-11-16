import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    color: Theme.darkMode ? "#997654" : "#A48B65"
    radius: 3
    border.width: 1
    border.color: Theme.darkMode ? "#E2C18B" : "#7E6040"
    Rectangle { anchors.horizontalCenter: parent.horizontalCenter; y:Math.max(0,parent.height/2-5); width:Math.min(parent.width-4,2); height:Math.min(parent.height-4,10); color:"#F6E4C4" }
}
