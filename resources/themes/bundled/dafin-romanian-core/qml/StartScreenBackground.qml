import QtQuick
import Drift

// Romanian Core: original visual-only override. No input handling, network,
// filesystem access, or executable imports. Colors follow the selected theme side.
Rectangle {
    id: root
    property var control
    readonly property color ornament: Theme.darkMode ? "#D66B68" : "#A7333A"
    readonly property color gold: Theme.darkMode ? "#D7AF70" : "#957045"
    readonly property color base: Theme.c.startScreen.background
    radius: 0
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
