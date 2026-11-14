import QtQuick
import Drift

// Dafin: the cells of the split screen under the playhead, outlined and numbered on the preview, with
// "Drop media here" on the empty ones — shown while that split screen is selected or while a video
// or picture is being dragged, never while playing (AppController::splitScreenOverlay). It only
// draws: drops onto a cell are PreviewDropOverlay's (planDrop finds the cell and fills it).
Canvas {
    id: root

    property int revision: 0
    readonly property bool dragging: EditorState.draggingAssetIndex >= 0
    readonly property var overlay: {
        void root.revision
        void EditorState.tracksRevision
        void EditorState.playheadSeconds
        if (EditorState.playing)
            return ({})
        return EditorState.splitScreenOverlay()
    }
    readonly property var cells: root.overlay.cells || []

    visible: root.cells.length > 0 && (!!root.overlay.selected || root.dragging)

    Connections {
        target: EditorState
        function onSelectionChanged() { root.revision++ }
    }

    onOverlayChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const accent = Theme.c.effectTemplateCard.accent
        const numberSize = Math.max(11, Math.min(22, height * 0.05))
        ctx.textAlign = "center"
        ctx.textBaseline = "middle"
        for (let i = 0; i < root.cells.length; ++i) {
            const cell = root.cells[i]
            const points = cell.points || []
            if (points.length < 3)
                continue
            let minX = 1, maxX = 0, minY = 1, maxY = 0
            ctx.beginPath()
            for (let j = 0; j < points.length; ++j) {
                const x = points[j].x * width
                const y = points[j].y * height
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
            ctx.lineWidth = 1.5
            ctx.strokeStyle = Qt.rgba(accent.r, accent.g, accent.b, cell.filled ? 0.55 : 0.9)
            ctx.stroke()

            const cx = (minX + maxX) / 2 * width
            const cy = (minY + maxY) / 2 * height
            ctx.fillStyle = Qt.rgba(0, 0, 0, 0.55)
            ctx.beginPath()
            ctx.arc(cx, cy - (cell.filled ? 0 : numberSize * 0.7), numberSize * 0.8, 0, Math.PI * 2)
            ctx.fill()
            ctx.fillStyle = "#ffffff"
            ctx.font = "600 " + Math.round(numberSize) + "px sans-serif"
            ctx.fillText(String(cell.number), cx, cy - (cell.filled ? 0 : numberSize * 0.7))
            if (!cell.filled) {
                ctx.font = Math.round(numberSize * 0.6) + "px sans-serif"
                ctx.fillStyle = Qt.rgba(1, 1, 1, 0.85)
                ctx.fillText(qsTr("Drop media here"), cx, cy + numberSize * 0.6)
            }
        }
    }
}
