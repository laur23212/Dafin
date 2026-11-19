import QtQuick
import Drift
// Makes whatever it covers draggable as an asset. The gesture follows the device of each press, not
// the form factor: a mouse/touchpad/stylus clicks for the card's primary action (tapped), drags as a
// platform drag (the mime type comes from AssetDrag) and right-clicks for contextMenuRequested; a
// finger presses-and-holds to lift into TouchDrag and taps for contextMenuRequested when the card
// has a menu (hasContextMenu), else for tapped.
//
// The Drag attached properties sit on this item and the handlers on a child: a handler on the item
// that owns Drag.Automatic re-enters QDrag::exec through its own active binding.
Item {
    id: root

    required property string kind
    required property var payload
    property string label: ""
    property string thumbnail: ""
    property string glyph: ""
    property alias liftEnabled: lift.liftEnabled
    // Dafin: when set, the card drags as that file (text/uri-list), exactly like a file dragged
    // in from the file manager: the timeline and the preview import it and place it where it
    // lands. Used by Lottie stickers and titles, which are files rather than catalog ids.
    property url fileUrl: ""
    readonly property bool fileDrag: fileUrl.toString().length > 0
    // Dafin: the card's tab and item id in the "My" area (AssetGroups), so it can be reordered in
    // favorites or a group, or dropped on a group chip. Carried apart from the payload, which is
    // not always the id (a video sticker drags as media).
    property string groupTab: ""
    property string groupItem: ""
    // Dafin: an online catalog item not downloaded yet. The timeline downloads it on the drop and
    // places it where it was let go (AssetDrag.onlineKey).
    property var onlineItem: null
    readonly property bool onlineDrag: !!onlineItem && !fileDrag

    readonly property bool active: mouseDrag.active || lift.lifted
    readonly property bool pressed: tap.pressed || lift.pressed
    readonly property bool hovered: hover.hovered

    // Whether a finger's tap should open the card's menu instead of running its primary action.
    property bool hasContextMenu: false

    signal tapped()
    signal doubleTapped()
    signal contextMenuRequested()

    // Dafin: a tap or an accepted drop counts as a use of the card (Recently Used).
    function noteUsed() {
        if (root.groupTab.length > 0 && root.groupItem.length > 0)
            AssetGroups.noteUsed(root.groupTab, root.groupItem)
    }
    Drag.onDragFinished: (dropAction) => {
        if (dropAction !== Qt.IgnoreAction)
            root.noteUsed()
    }

    Drag.active: mouseDrag.active
    Drag.dragType: Drag.Automatic
    Drag.supportedActions: Qt.CopyAction
    Drag.keys: root.fileDrag ? ["text/uri-list"]
             : root.onlineDrag ? [AssetDrag.onlineKey]
             : [AssetDrag.mimeKey(root.kind)]
    Drag.mimeData: root.fileDrag ? ({ "text/uri-list": root.fileUrl.toString() })
                 : root.onlineDrag ? ({ "application/x-dafin-online": String(root.onlineItem.key || ""),
                                        "application/x-drift-asset-label": root.label })
                 : AssetDrag.mimeData(root.kind, root.payload, root.label)
    // Dafin: ghost card under the pointer while dragging (see AssetDragGhost).
    AssetDragGhost {
        id: ghost
        thumbnail: root.thumbnail.length > 0 ? EditorState.imageUrl(root.thumbnail) : ""
        label: root.label
        glyph: root.glyph
    }
    Drag.imageSource: ghost.imageUrl
    Drag.hotSpot.x: ghost.ready ? ghost.hotX : width / 2
    Drag.hotSpot.y: ghost.ready ? ghost.hotY : height / 2

    Item {
        anchors.fill: parent

        HoverHandler {
            id: hover
            cursorShape: mouseDrag.active ? Qt.ClosedHandCursor : Qt.PointingHandCursor
        }

        Connections {
            target: tap
            function onPressedChanged() {
                if (tap.pressed)
                    ghost.prepare()
            }
        }

        TapHandler {
            id: tap
            acceptedButtons: Qt.LeftButton
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onTapped: { root.noteUsed(); root.tapped() }
            onDoubleTapped: root.doubleTapped()
        }

        TapHandler {
            acceptedButtons: Qt.RightButton
            onTapped: root.contextMenuRequested()
        }

        DragHandler {
            id: mouseDrag
            target: null
            acceptedButtons: Qt.LeftButton
            // Not the touchscreen: a finger on a desktop touchscreen scrolls the grid.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.Stylus
            onActiveChanged: {
                // A file drag is marked "file" so the assets panel knows it came from inside
                // and does not file it into My Lotties again.
                if (active) {
                    AssetGroups.beginDrag(root.groupTab, root.groupItem)
                    if (root.onlineDrag)
                        AssetDrag.begin("online", root.onlineItem.key || "", root.label, root.onlineItem)
                    else
                        AssetDrag.begin(root.fileDrag ? "file" : root.kind, root.fileDrag ? root.fileUrl : root.payload, root.label)
                } else {
                    AssetDrag.end()
                    AssetGroups.endDrag()
                }
            }
        }

        TouchLiftArea {
            id: lift
            dragKind: root.kind
            payload: root.payload
            label: root.label
            thumbnail: root.thumbnail
            glyph: root.glyph
            onLiftTapped: root.hasContextMenu ? root.contextMenuRequested() : root.tapped()
        }
    }
}
