import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#D66B68" : "#A7333A"
    readonly property color gold: Theme.darkMode ? "#D7AF70" : "#957045"
    readonly property color base: Theme.c.timelineToolbar.background
    radius: 8
    color: root.base
    border.width: 0
    border.color: root.gold
    clip: true
    
    Rectangle { x: 0; y: Math.max(0, root.height-2); width: root.width; height: 2; color: root.ornament; opacity: 0.58 }
    Rectangle { x: 2; y: 0; width: 2; height: root.height; color: root.gold; opacity: 0.27 }

    
    // Traditional geometric embroidery inspired by motifs on the ie (altiță).
    Item {
        x: 8; y: Math.max(0, root.height - 14); width: Math.max(0, root.width - 16); height: 10; clip: true
        Repeater {
            model: Math.max(0, Math.ceil(parent.width / 38))
            delegate: Item {
                x: index * 38; y: 1; width: 35; height: 8
                Rectangle { x: 4; y: 1; width: 7; height: 7; rotation: 45; color: "transparent"; border.width: 1; border.color: root.ornament }
                Rectangle { x: 6; y: 3; width: 3; height: 3; rotation: 45; color: root.ornament }
                Rectangle { x: 22; y: 3; width: 5; height: 1; color: root.gold }
                Rectangle { x: 24; y: 1; width: 1; height: 5; color: root.gold }
            }
        }
    }

    
}
