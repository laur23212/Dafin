pragma Singleton
import QtQuick
import Drift

// Every kind of asset a browser can drag, and the desktop half of dragging one.
//
// The phone lifts cards into TouchDrag; the desktop uses a platform drag (Drag.Automatic), which
// carries a mime type per kind. Both name kinds from the list below, and both drop targets hand
// the kind and payload to AppController::planAssetDrop / dropAsset, so a kind means the same
// thing wherever it lands.
QtObject {
    id: assetDrag

    // media is the asset index as text (and keeps text/plain, which is also how a dropped path
    // arrives); every other kind carries a catalog id.
    readonly property var mimeKeys: ({
        "media": "text/plain",
        "effect": "application/x-drift-effect",
        "audioEffect": "application/x-drift-audio-effect",
        "template": "application/x-drift-effect-template",
        "effectStack": "application/x-drift-effect-stack",
        "transition": "application/x-drift-transition",
        // Dafin: a clip In/Out animation chip; the payload is "animIn:<kind>" or "animOut:<kind>".
        "animation": "application/x-drift-animation",
        "shape": "application/x-drift-shape",
        "mask": "application/x-drift-mask",
        "sticker": "application/x-drift-sticker",
        "emoji": "application/x-drift-emoji",
        "textStyle": "application/x-drift-text-style",
        "adjustment": "application/x-drift-adjustment",
        // Dafin: a .mogrt from the Titles grid; the payload is the template file's path.
        "mogrt": "application/x-dafin-mogrt",
        // Dafin: a split screen card from Templates; the payload is the layout id.
        "splitScreen": "application/x-dafin-split-screen",
        // Dafin: a Scene Title from Titles › Scenes; the payload is the scene id.
        "sceneTitle": "application/x-dafin-scene-title"
    })
    readonly property string labelKey: "application/x-drift-asset-label"
    // Dafin: an online card not downloaded yet (stock video, music, a sound effect). The mime data
    // carries its key; the catalog item itself rides in `current.item`, because the drop starts
    // the download and needs all of it. Kept out of mimeKeys on purpose: only the timeline takes
    // it, so nothing else (effects on clips, the assets panel) reacts to it.
    readonly property string onlineKey: "application/x-dafin-online"

    // The drag in flight from this app, if any. A platform drop may not see the mime data until
    // release (Wayland withholds it), so targets read the payload from here first.
    property var current: ({ "kind": "", "payload": "", "label": "", "item": null })
    readonly property bool active: current.kind !== ""

    function mimeKey(kind) {
        return mimeKeys[kind] || ""
    }

    function mimeData(kind, payload, label) {
        const data = {}
        data[mimeKey(kind)] = String(payload)
        data[labelKey] = label || ""
        return data
    }

    function allKeys() {
        const keys = []
        for (const kind in mimeKeys)
            keys.push(mimeKeys[kind])
        return keys
    }

    // Keys of the kinds that create a clip rather than act on one.
    function placeableKeys() {
        return ["shape", "sticker", "emoji", "textStyle", "adjustment", "mogrt", "splitScreen", "sceneTitle"].map(k => mimeKeys[k])
    }

    function kindFromKeys(keys) {
        for (const kind in mimeKeys) {
            if (kind !== "media" && keys.indexOf(mimeKeys[kind]) >= 0)
                return kind
        }
        return keys.indexOf("text/plain") >= 0 ? "media" : ""
    }

    function payloadFromDrop(drop, kind) {
        if (current.kind === kind)
            return current.payload
        return drop.getDataAsString(mimeKey(kind))
    }

    function labelFromDrop(drop, kind) {
        if (current.kind === kind)
            return current.label
        return drop.getDataAsString(labelKey)
    }

    function begin(kind, payload, label, item) {
        current = { "kind": kind, "payload": String(payload), "label": label || "", "item": item || null }
        if (kind === "media")
            EditorState.draggingAssetIndex = Number(payload)
    }

    // Deferred: the drop handler runs after the drag source's active flag drops, and still
    // needs `current` to read the payload.
    function end() {
        Qt.callLater(function() {
            if (assetDrag.current.kind === "media")
                EditorState.draggingAssetIndex = -1
            assetDrag.current = { "kind": "", "payload": "", "label": "", "item": null }
        })
    }
}
