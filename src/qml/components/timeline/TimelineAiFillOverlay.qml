import QtQuick
import QtQuick.Shapes
import Drift

// Dafin AI Fill: the timeline tool that marks a space and asks the AI to fill it with an
// animation (Photoshop's Generative Fill, on a timeline). Drag across the tracks to mark the
// space (a click marks 3 s), describe it in the card that opens beside it, and the result lands
// exactly there. The request can reach beyond the space ("…and arrows on the next clip"): the AI
// has every tool the chat has. Failsafes: the edges stay draggable and Redraw starts over (the
// description is kept); every new take is a version of the same clip, so "Change it" and "Try
// again" never lose one; everything else it does is ordinary undoable edits.
Item {
    id: fill

    property var panel
    property Flickable view

    x: 0
    y: Theme.timelineRulerHeight + Theme.timelineBookmarkRowHeight
    width: parent.width
    height: Math.max(panel.totalTracksHeightCached, view.height - view.headerHeight)
    z: 21
    visible: toolOn || regionShown

    readonly property bool toolOn: panel.timelineTool === "aiFill"

    // ---- The marked space ------------------------------------------------------------------
    property bool regionShown: false
    property real regionStart: 0
    property real regionEnd: 0
    property int regionTrack: -1       // top row drawn over; -1 = anywhere
    property int regionBottomTrack: -1

    // ---- Where the card is at --------------------------------------------------------------
    // "prompt" (describe it), "working" (the AI is on it), "done" (a result, with its versions),
    // "error" (it did not work; nothing changed unless a version was placed).
    property string phase: ""
    property string mode: "new"         // "new" space, or "change" the AI Fill clip below
    property string clipId: ""          // the AI Fill clip this space shows
    property bool submitted: false      // waiting on Assistant.fill for our request
    property string promptText: ""      // kept across Redraw and failures
    property string changeText: ""      // what "Change it" asked for, kept if it fails
    property string note: ""            // one-line explanation shown at the top of the card
    property var versionsInfo: ({ ok: false })

    // ---- Drawing ---------------------------------------------------------------------------
    property bool drawing: false
    property real anchorSeconds: 0
    property real anchorY: 0
    property real pressX: 0

    readonly property real pxPerSecond: panel.pxPerSecond
    readonly property real regionX: regionStart * pxPerSecond
    readonly property real regionW: Math.max(2, (regionEnd - regionStart) * pxPerSecond)
    readonly property real regionY: regionTrack >= 0 ? panel.trackOffsetY(regionTrack) : 0
    readonly property real regionH: {
        if (regionTrack < 0 || regionBottomTrack < 0)
            return Math.max(panel.totalTracksHeightCached, 48)
        const bottom = panel.trackOffsetY(regionBottomTrack) + panel.trackHeight(regionBottomTrack)
        return Math.max(12, bottom - regionY)
    }
    readonly property bool editable: regionShown && !drawing && (phase === "prompt" || phase === "error")
    readonly property real minLength: 0.25

    function clock(seconds) {
        const s = Math.max(0, seconds)
        const m = Math.floor(s / 60)
        const r = s - m * 60
        return m + ":" + (r < 10 ? "0" : "") + r.toFixed(1)
    }
    function lengthText() {
        return qsTr("%1 s").arg((regionEnd - regionStart).toFixed(1))
    }
    function snapped(px) {
        return EditorState.snapTime(Math.max(0, px) / pxPerSecond)
    }
    function rowsFor(y1, y2) {
        const top = panel.trackIndexAtY(Math.min(y1, y2))
        const bottom = panel.trackIndexAtY(Math.max(y1, y2))
        regionTrack = top
        regionBottomTrack = top < 0 ? -1 : (bottom < 0 ? panel.tracks.length - 1 : bottom)
    }

    function reset() {
        drawing = false
        regionShown = false
        phase = ""
        mode = "new"
        clipId = ""
        submitted = false
        note = ""
        versionsInfo = { ok: false }
        Assistant.dismissFill()
    }
    // Back to an empty tool, keeping the description so a redrawn space can use it.
    function redraw() {
        if (phase === "working")
            return
        drawing = false
        regionShown = false
        phase = ""
        mode = "new"
        clipId = ""
        note = ""
        if (!toolOn)
            panel.timelineTool = "aiFill"
    }
    function close() {
        if (phase === "working")
            return
        reset()
        panel.timelineTool = ""
    }

    function refreshVersions() {
        versionsInfo = clipId !== "" ? EditorState.aiFillVersions(clipId) : { ok: false }
        if (clipId !== "" && !versionsInfo.ok) {
            // The clip went away (deleted or undone): the space and the description stay.
            clipId = ""
            mode = "new"
            if (phase === "done") {
                phase = "prompt"
                note = qsTr("The animation was removed. Generate again, or redraw the space.")
            }
        } else if (versionsInfo.ok && phase === "done") {
            regionStart = versionsInfo.start
            regionEnd = versionsInfo.end
            regionTrack = versionsInfo.track
            regionBottomTrack = versionsInfo.track
        }
    }

    // Shows an AI Fill clip's card: its versions, Change it, Try again.
    function openClip(id) {
        const info = EditorState.aiFillVersions(id)
        if (!info.ok)
            return false
        clipId = id
        mode = "new"
        note = ""
        phase = "done"
        regionShown = true
        versionsInfo = info
        regionStart = info.start
        regionEnd = info.end
        regionTrack = info.track
        regionBottomTrack = info.track
        return true
    }
    function aiClipAt(x, y) {
        const t = panel.trackIndexAtY(y)
        const c = t >= 0 ? panel.clipIndexAtPosition(t, x) : -1
        if (c < 0)
            return ""
        const clip = panel.tracks[t].clips[c]
        return clip && clip.vector && clip.vector.aiVersions > 0 ? clip.id : ""
    }

    function currentPrompt() {
        if (!versionsInfo.ok)
            return promptText
        const v = versionsInfo.versions[versionsInfo.current]
        return v ? v.prompt : promptText
    }

    // retry: a different take on the current version's request.
    function generate(text, retry) {
        const prompt = (text || "").trim()
        if (prompt.length === 0 || phase === "working")
            return
        if (mode === "change")
            changeText = prompt
        else if (!retry)
            promptText = prompt
        note = ""
        submitted = true
        Assistant.generateFill({
            prompt: prompt,
            start: regionStart,
            end: regionEnd,
            track: regionTrack,
            clipId: (mode === "change" || retry) ? clipId : "",
            retry: !!retry
        })
        syncFromAssistant()
    }
    function syncFromAssistant() {
        if (!submitted)
            return
        const f = Assistant.fill
        switch (f.state) {
        case "working":
            phase = "working"
            break
        case "done":
            submitted = false
            // Empty when the request was about other places: a clip already here stays linked.
            if (f.clipId)
                clipId = f.clipId
            mode = "new"
            phase = "done"
            note = ""
            refreshVersions()
            break
        case "stopped":
            submitted = false
            if (clipId !== "") {
                mode = "new"
                phase = "done"
            } else {
                phase = "prompt"
            }
            note = qsTr("Stopped. Nothing was added.")
            if (clipId !== "")
                refreshVersions()
            break
        case "error":
            submitted = false
            phase = "error"
            break
        }
    }
    function switchVersion(step) {
        if (!versionsInfo.ok)
            return
        const next = versionsInfo.current + step
        if (next < 0 || next >= versionsInfo.versions.length)
            return
        const error = EditorState.switchAiFillVersion(clipId, next)
        note = error
        refreshVersions()
    }

    onToolOnChanged: {
        if (toolOn) {
            // A selected AI Fill clip opens straight to its versions.
            const t = panel.selectedTrack
            const c = panel.selectedClip
            if (!regionShown && t >= 0 && c >= 0 && t < panel.tracks.length && c < panel.tracks[t].clips.length) {
                const clip = panel.tracks[t].clips[c]
                if (clip && clip.vector && clip.vector.aiVersions > 0)
                    openClip(clip.id)
            }
        } else if (phase !== "working") {
            reset()
        }
    }

    Connections {
        target: Assistant
        function onFillChanged() { fill.syncFromAssistant() }
    }
    Connections {
        target: EditorState
        function onTracksChanged() {
            if (fill.clipId !== "" && !fill.submitted)
                fill.refreshVersions()
        }
    }

    // ---- Pointer: draw a space, or open an AI Fill clip --------------------------------------
    MouseArea {
        id: drawArea
        anchors.fill: parent
        enabled: fill.toolOn && fill.phase !== "working"
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.CrossCursor
        preventStealing: true

        onPressed: (mouse) => {
            fill.pressX = mouse.x
            fill.anchorY = mouse.y
            fill.anchorSeconds = fill.snapped(mouse.x)
            fill.drawing = true
            fill.regionShown = true
            fill.phase = ""
            fill.clipId = ""
            fill.mode = "new"
            fill.note = ""
            fill.regionStart = fill.anchorSeconds
            fill.regionEnd = fill.anchorSeconds
            fill.rowsFor(mouse.y, mouse.y)
        }
        onPositionChanged: (mouse) => {
            if (!fill.drawing)
                return
            const at = fill.snapped(mouse.x)
            fill.regionStart = Math.min(fill.anchorSeconds, at)
            fill.regionEnd = Math.max(fill.anchorSeconds, at)
            fill.rowsFor(fill.anchorY, mouse.y)
        }
        onReleased: (mouse) => {
            if (!fill.drawing)
                return
            fill.drawing = false
            if (Math.abs(mouse.x - fill.pressX) < 4) {
                // A click: on an AI Fill clip it opens that clip; elsewhere it marks 3 s.
                const id = fill.aiClipAt(mouse.x, mouse.y)
                if (id !== "" && fill.openClip(id))
                    return
                fill.regionStart = fill.anchorSeconds
                fill.regionEnd = fill.anchorSeconds + 3.0
            } else if (fill.regionEnd - fill.regionStart < fill.minLength) {
                fill.regionEnd = fill.regionStart + fill.minLength
            }
            fill.phase = "prompt"
        }
        onCanceled: {
            fill.drawing = false
            if (fill.phase === "")
                fill.regionShown = false
        }
    }

    // ---- First hint, before anything is drawn -------------------------------------------------
    Rectangle {
        visible: fill.toolOn && !fill.regionShown
        x: fill.view.contentX + Theme.spacingMd
        y: Math.max(0, fill.view.contentY) + Theme.spacingSm
        width: hintRow.implicitWidth + Theme.spacingMd * 2
        height: hintRow.implicitHeight + Theme.spacingSm * 2
        radius: height / 2
        color: Theme.panelBackground
        border.color: Theme.accent
        opacity: 0.96
        Row {
            id: hintRow
            anchors.centerIn: parent
            spacing: Theme.spacingSm
            IconGlyph {
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.sparkles
                iconSize: 14
                iconColor: Theme.accent
            }
            ThemedLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Drag across the timeline to mark where the animation goes. Click an AI clip to see its versions. Esc leaves.")
            }
        }
    }

    // ---- The marked space ---------------------------------------------------------------------
    Item {
        id: regionRect
        visible: fill.regionShown
        x: fill.regionX
        y: fill.regionY
        width: fill.regionW
        height: fill.regionH

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSm
            color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b,
                           fill.phase === "done" ? 0.06 : 0.16)
            clip: true

            // Shimmer while the AI works: a soft band sweeping left to right.
            Rectangle {
                id: shimmer
                visible: fill.phase === "working"
                width: Math.max(40, parent.width * 0.45)
                height: parent.height
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: "transparent" }
                    GradientStop { position: 0.5; color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.35) }
                    GradientStop { position: 1.0; color: "transparent" }
                }
                NumberAnimation on x {
                    running: shimmer.visible
                    loops: Animation.Infinite
                    from: -shimmer.width
                    to: regionRect.width
                    duration: 1400
                }
            }
        }

        // Marching-ants outline: dashes that keep moving, so the space reads as "selected".
        Shape {
            anchors.fill: parent
            ShapePath {
                id: ants
                strokeColor: Theme.accent
                strokeWidth: fill.phase === "done" ? 1 : 1.5
                fillColor: "transparent"
                strokeStyle: ShapePath.DashLine
                dashPattern: [4, 3]
                startX: 0.5; startY: 0.5
                PathLine { x: regionRect.width - 0.5; y: 0.5 }
                PathLine { x: regionRect.width - 0.5; y: regionRect.height - 0.5 }
                PathLine { x: 0.5; y: regionRect.height - 0.5 }
                PathLine { x: 0.5; y: 0.5 }
                NumberAnimation on dashOffset {
                    running: fill.regionShown && fill.phase !== "done"
                    loops: Animation.Infinite
                    from: 0
                    to: -14
                    duration: 900
                }
            }
        }

        // Length label, on the space's top-left corner.
        Rectangle {
            x: 0
            y: Math.max(-height - 2, -regionRect.y)
            width: lengthLabel.implicitWidth + Theme.spacingSm * 2
            height: lengthLabel.implicitHeight + 4
            radius: Theme.radiusSm
            color: Theme.accent
            ThemedLabel {
                id: lengthLabel
                anchors.centerIn: parent
                color: Theme.accentForeground
                font.pixelSize: Theme.fontSizeXs
                text: fill.drawing ? fill.lengthText()
                                   : fill.lengthText() + "  ·  " + fill.clock(fill.regionStart) + "–" + fill.clock(fill.regionEnd)
            }
        }

        // Edge handles: drag to adjust the space instead of drawing it again.
        Repeater {
            model: fill.editable ? 2 : 0
            delegate: MouseArea {
                id: edge
                required property int index
                readonly property bool left: index === 0
                width: 10
                height: regionRect.height
                x: left ? -width / 2 : regionRect.width - width / 2
                cursorShape: Qt.SizeHorCursor
                preventStealing: true
                hoverEnabled: true
                onPositionChanged: (mouse) => {
                    if (!pressed)
                        return
                    const px = mapToItem(fill, mouse.x, mouse.y).x
                    const at = fill.snapped(px)
                    if (left)
                        fill.regionStart = Math.max(0, Math.min(at, fill.regionEnd - fill.minLength))
                    else
                        fill.regionEnd = Math.max(at, fill.regionStart + fill.minLength)
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 4
                    height: Math.min(28, parent.height - 6)
                    radius: 2
                    color: Theme.accent
                    opacity: edge.containsMouse || edge.pressed ? 1.0 : 0.7
                }
            }
        }
    }

    TimelineAiFillCard {
        host: fill
        anchorItem: regionRect
    }
}
