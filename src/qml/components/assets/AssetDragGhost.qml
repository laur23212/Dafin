import QtQuick
import Drift

// Dafin: the card that hangs off the pointer during a native (QDrag) asset drag, as in Filmora: a
// small thumbnail card with a green plus. Native drags show no picture unless Drag.imageSource
// is set, and the source must exist before the drag starts, so it is drawn once into an image
// (grabToImage) when the card is pressed. Use:
//   AssetDragGhost { id: ghost; thumbnail: ...; label: ...; glyph: ... }
//   Drag.imageSource: ghost.imageUrl
//   Drag.hotSpot.x: ghost.ready ? ghost.hotX : <old>      (the pointer sits mid-card)
//   on press: ghost.prepare()
// Until the image is ready (or if it fails) the drag looks as it did before.
Item {
    id: g

    // Image file URL already resolved with EditorState.imageUrl, or "".
    property url thumbnail: ""
    property string label: ""
    property string glyph: ""

    readonly property real ghostWidth: 104
    readonly property real ghostHeight: 72
    // Pointer position inside the picture: its centre, the same hot spot the cards used before
    // (Wayland compositors deliver the drop point relative to it).
    readonly property real hotX: ghostWidth / 2
    readonly property real hotY: ghostHeight / 2
    property bool ready: false
    property url imageUrl: ""

    width: 0
    height: 0

    // A recycled grid card or a changed thumbnail must not keep dragging the old picture.
    function reset() {
        ready = false
        imageUrl = ""
    }
    onThumbnailChanged: reset()
    onLabelChanged: reset()

    function prepare() {
        if (ready || loader.active)
            return
        loader.active = true
        Qt.callLater(function () {
            if (!loader.item)
                return
            loader.item.grabToImage(function (result) {
                if (result && result.url.toString().length > 0) {
                    g.imageUrl = result.url
                    g.ready = true
                }
                loader.active = false
            })
        })
    }

    Loader {
        id: loader
        active: false
        // Far outside the card so it is never seen; grabToImage still renders it.
        x: -4000
        y: -4000
        sourceComponent: Item {
            width: g.ghostWidth
            height: g.ghostHeight
            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                radius: Theme.c.touchDragGhost.radius
                color: Theme.c.touchDragGhost.background
                border.width: Theme.c.touchDragGhost.borderWidth
                border.color: Theme.c.touchDragGhost.border
                clip: true
                opacity: 0.92
                Image {
                    id: thumb
                    anchors.fill: parent
                    visible: g.thumbnail.toString().length > 0 && status === Image.Ready
                    source: g.thumbnail
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: false
                }
                IconGlyph {
                    anchors.centerIn: parent
                    visible: !thumb.visible && g.glyph.length > 0
                    glyph: g.glyph
                    iconSize: Theme.iconSizeXl
                    iconColor: Theme.c.touchDragGhost.icon
                }
                Text {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 3
                    text: g.label
                    visible: text.length > 0
                    color: Theme.c.touchDragGhost.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.Medium
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }
            }
            // The green "will be added" plus.
            Rectangle {
                width: 18
                height: 18
                radius: 9
                x: 0
                y: parent.height - height
                color: "#2fd6a5"
                border.width: 2
                border.color: "#1b1f24"
                Text {
                    anchors.centerIn: parent
                    text: "+"
                    color: "#10231d"
                    font.pixelSize: 14
                    font.bold: true
                }
            }
        }
    }
}
