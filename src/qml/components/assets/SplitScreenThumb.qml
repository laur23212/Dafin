import QtQuick
import Drift

// Dafin: a split screen layout drawn the way its card shows it (Templates › Split Screen, and the
// layout picker of the Split Screen tab): the cells tinted and numbered, or — while `playing` — the
// template's entrance, each cell unfolding in from its edge, centre line or centre one after another,
// then the border drawn between them, looping. `layout` is one entry of
// EditorState.splitScreenLayouts(): cells with points, the polygon they unfold from, delay, duration.
Canvas {
    id: root

    property var layout: ({})
    property bool playing: false
    property bool numbered: true
    property real time: 0

    readonly property real loopSeconds: (root.layout && root.layout.entrance ? root.layout.entrance : 1) + 1.2
    readonly property var palette: ["#4e79a7", "#f28e2b", "#e15759", "#76b7b2", "#59a14f", "#edc948",
                                    "#b07aa1", "#ff9da7", "#9c755f"]

    function ease(t) {
        return 1 - Math.pow(1 - t, 3)
    }

    onLayoutChanged: requestPaint()
    onPlayingChanged: {
        root.time = 0
        requestPaint()
    }
    onTimeChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()

    NumberAnimation on time {
        running: root.playing && root.visible
        from: 0
        to: root.loopSeconds
        duration: root.loopSeconds * 1000
        loops: Animation.Infinite
    }

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const cells = (root.layout && root.layout.cells) || []
        const accent = Theme.c.effectTemplateCard.accent
        if (root.playing) {
            ctx.fillStyle = "#000000"
            ctx.fillRect(0, 0, width, height)
        }
        ctx.font = "600 " + Math.max(8, Math.round(height * 0.2)) + "px sans-serif"
        ctx.textAlign = "center"
        ctx.textBaseline = "middle"
        const whole = root.playing && root.time >= (root.layout.entrance || 0)

        for (let i = 0; i < cells.length; ++i) {
            const cell = cells[i]
            const points = cell.points || []
            const from = cell.from || points
            let k = 1
            if (root.playing)
                k = ease(Math.max(0, Math.min(1, (root.time - cell.delay) / Math.max(0.01, cell.duration))))
            if (k <= 0)
                continue

            ctx.beginPath()
            let minX = 1, maxX = 0, minY = 1, maxY = 0
            for (let j = 0; j < points.length; ++j) {
                const x = (from[j].x + (points[j].x - from[j].x) * k) * width
                const y = (from[j].y + (points[j].y - from[j].y) * k) * height
                if (j === 0)
                    ctx.moveTo(x, y)
                else
                    ctx.lineTo(x, y)
                minX = Math.min(minX, points[j].x)
                maxX = Math.max(maxX, points[j].x)
                minY = Math.min(minY, points[j].y)
                maxY = Math.max(maxY, points[j].y)
            }
            ctx.closePath()

            if (root.playing) {
                ctx.fillStyle = root.palette[i % root.palette.length]
                ctx.fill()
                if (whole) {
                    ctx.lineWidth = 1.5
                    ctx.strokeStyle = "#ffffff"
                    ctx.stroke()
                }
            } else {
                ctx.fillStyle = Qt.rgba(accent.r, accent.g, accent.b, i % 2 === 0 ? 0.30 : 0.18)
                ctx.fill()
                ctx.lineWidth = 2
                ctx.strokeStyle = Theme.c.effectTemplateCard.background
                ctx.stroke()
                if (root.numbered && points.length > 0) {
                    ctx.fillStyle = Theme.c.effectTemplateCard.foregroundOnMedia
                    ctx.fillText(String(i + 1), (minX + maxX) / 2 * width, (minY + maxY) / 2 * height)
                }
            }
        }
    }
}
