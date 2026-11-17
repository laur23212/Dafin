import QtQuick
import Drift

// Romanian Core (tricolor): original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#E0243A" : "#CE1126"
    readonly property color gold: Theme.darkMode ? "#E3BE1E" : "#9C7F00"
    readonly property color yellow: "#FCD116"
    readonly property color blue: Theme.darkMode ? "#2455C9" : "#002B7F"
    readonly property color base: Theme.c.startScreen.background
    radius: 0
    color: root.base
    border.width: 0
    border.color: root.gold
    clip: true
    
    // Flag order, left to right: blue | yellow | red.
    Row {
        x: 0; y: Math.max(0, root.height - 3); width: root.width; height: 3
        Rectangle { width: parent.width / 3; height: 3; color: root.blue }
        Rectangle { width: parent.width / 3; height: 3; color: root.yellow }
        Rectangle { width: parent.width / 3; height: 3; color: root.ornament }
    }

    
    // "Dinti de lup" (wolf teeth): the sawtooth border of Romanian weaving and carved gates.
    Item {
        x: 6; y: Math.max(0, root.height - 12); width: Math.max(0, root.width - 12); height: 7; clip: true
        Repeater {
            model: Math.max(0, Math.ceil(parent.width / 14))
            delegate: Rectangle {
                x: index * 14 + 2; y: 3; width: 7; height: 7; rotation: 45
                color: index % 2 === 0 ? root.yellow : root.ornament
                opacity: 0.85
            }
        }
    }

    
    // Ceramic sun-disc/rosette inspired by Horezu pottery. Kept in the corner,
    // translucent, and behind the functional start-screen content.
    Item {
        id: ceramicDisc
        anchors.right: parent.right; anchors.bottom: parent.bottom
        anchors.rightMargin: 28; anchors.bottomMargin: 28
        width: Math.min(196, root.width * 0.21); height: width
        opacity: 0.23
        Rectangle { anchors.fill: parent; radius: width/2; color: "transparent"; border.width: 5; border.color: root.gold }
        Rectangle { anchors.centerIn: parent; width: parent.width*.73; height: width; radius: width/2; color: "transparent"; border.width: 3; border.color: root.ornament }
        Repeater { model: 12; delegate: Rectangle {
            x: ceramicDisc.width/2 + Math.cos(index*Math.PI/6)*ceramicDisc.width*.38 - 4
            y: ceramicDisc.height/2 + Math.sin(index*Math.PI/6)*ceramicDisc.height*.38 - 7
            width: 8; height: 14
            rotation: index*30 + 90
            color: root.gold; radius: 4
        } }
        Rectangle { anchors.centerIn: parent; width: parent.width*0.3; height: width; radius: width/2; color: root.ornament }
        Rectangle { anchors.centerIn: parent; width: parent.width*0.1; height: width; radius: width/2; color: root.gold }
    }
    Text {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.leftMargin: 24; anchors.bottomMargin: 16
        font.family: "Georgia"; font.italic: true; font.pixelSize: 18
        color: root.gold; opacity: .56
        text: "cu dor, din România"
    }

}
