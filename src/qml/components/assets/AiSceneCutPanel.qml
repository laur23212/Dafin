import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Smart scene cut. Finds the shot changes in the selected video clip
// (EditorState.scenes, the same engine as the Scenes tab), shows where it would cut, and splits
// the clip there as one undo step. No model to download.
AiToolPage {
    id: root

    description: qsTr("Finds where the shot changes in the selected video clip, shows the cut points, then splits the clip there in one step. Runs on this computer; nothing to download.")

    readonly property var clip: EditorState.selectedClipData
    readonly property bool hasVideoClip: clip.kind === "video"
    readonly property real clipStart: clip.start !== undefined ? clip.start : 0
    readonly property real clipDuration: clip.duration !== undefined ? clip.duration : 0
    readonly property bool analysed: hasVideoClip && EditorState.scenes.list.length > 0
                                     && EditorState.scenes.clipId === clip.id
    // Timeline seconds of the cuts in the selected clip. Re-read when the analysis or the
    // selection changes, or the clip is moved or trimmed.
    readonly property var cuts: {
        void EditorState.scenes.list
        void root.clip
        return root.analysed ? EditorState.scenes.cutTimes(EditorState.selectedTrack, EditorState.selectedClip) : []
    }
    // Sensitivity moved since the scan: the cut points shown are from the old setting.
    property bool sensitivityChanged: false

    function timeLabel(seconds) {
        const s = Math.max(0, seconds)
        const m = Math.floor(s / 60)
        const rest = s - m * 60
        return m + ":" + (rest < 10 ? "0" : "") + rest.toFixed(1)
    }

    Connections {
        target: EditorState.scenes
        function onListChanged() { root.sensitivityChanged = false }
    }

    AiHint {
        visible: !root.hasVideoClip
        text: qsTr("Select a video clip on the timeline first.")
    }

    // --- sensitivity and Find scenes --------------------------------------------------
    Column {
        width: root.contentWidth
        spacing: Theme.spacingXs
        visible: root.hasVideoClip && !EditorState.scenes.detecting

        Text {
            text: qsTr("Sensitivity")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // Inverted, as in the Scenes tab: right finds more cuts, which a lower threshold does.
        ThemedSlider {
            width: root.contentWidth
            label: qsTr("Scene detection sensitivity")
            from: 4
            to: 60
            value: 64 - EditorState.scenes.threshold()
            valueFormatter: function (v) { return Number(64 - v).toFixed(0) }
            onMoved: {
                EditorState.scenes.setThreshold(64 - value)
                root.sensitivityChanged = root.analysed
            }
        }
    }

    ThemedButton {
        width: root.contentWidth
        visible: root.hasVideoClip && !EditorState.scenes.detecting
        text: root.analysed ? qsTr("Find scenes again") : qsTr("Find scenes")
        variant: root.analysed && !root.sensitivityChanged ? "secondary" : "primary"
        glyph: Theme.icons.listVideo
        tooltip: qsTr("Look for the shot changes in the selected clip. Nothing is cut yet.")
        onClicked: EditorState.scenes.detect(EditorState.selectedTrack, EditorState.selectedClip, false)
    }

    AiJobStatus {
        running: EditorState.scenes.detecting
        progress: EditorState.scenes.progress
        status: EditorState.scenes.status
        cancelTooltip: qsTr("Stop looking for scenes")
        onCancelRequested: EditorState.scenes.cancel()
    }

    // --- the cut points ----------------------------------------------------------------
    Column {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.analysed && !EditorState.scenes.detecting

        Text {
            width: root.contentWidth
            text: root.cuts.length === 0
                  ? qsTr("No shot changes inside this clip.")
                  : qsTr("%n cut point(s) · the clip becomes %1 pieces", "", root.cuts.length).arg(root.cuts.length + 1)
            color: Theme.c.subtitlesTab.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            wrapMode: Text.WordWrap
        }

        AiHint {
            visible: root.sensitivityChanged
            warning: true
            text: qsTr("Sensitivity changed. Find scenes again to update the cut points.")
        }

        // The clip as a bar, a tick at each cut. Click a tick to jump there.
        Rectangle {
            id: strip
            visible: root.cuts.length > 0 && root.clipDuration > 0
            width: root.contentWidth
            height: 28
            radius: Theme.radiusSm
            color: Theme.c.subtitlesTab.border
            readonly property real playheadX: {
                const t = (EditorState.playheadSeconds - root.clipStart) / Math.max(0.001, root.clipDuration)
                return t >= 0 && t <= 1 ? t * width : -10
            }

            Repeater {
                model: root.cuts
                delegate: Rectangle {
                    required property var modelData
                    x: Math.round((modelData - root.clipStart) / root.clipDuration * strip.width) - 1
                    width: 2
                    height: strip.height
                    color: Theme.c.subtitlesTab.accent

                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -4
                        cursorShape: Qt.PointingHandCursor
                        onClicked: EditorState.playheadSeconds = parent.modelData
                    }
                }
            }

            Rectangle {
                x: strip.playheadX
                width: 1
                height: strip.height
                color: Theme.c.subtitlesTab.foreground
                opacity: 0.6
            }
        }

        Flow {
            width: root.contentWidth
            spacing: Theme.spacingXs
            visible: root.cuts.length > 0

            Repeater {
                model: root.cuts
                ThemedChip {
                    required property var modelData
                    text: root.timeLabel(modelData)
                    onClicked: EditorState.playheadSeconds = modelData
                }
            }
        }

        ThemedButton {
            width: root.contentWidth
            visible: root.cuts.length > 0
            text: qsTr("Split at scene changes")
            variant: "primary"
            glyph: Theme.icons.scissors
            tooltip: qsTr("Cut the clip at every point above. One undo puts it back together.")
            onClicked: EditorState.scenes.splitAtScenes(EditorState.selectedTrack, EditorState.selectedClip)
        }
    }

    AiHint {
        visible: root.hasVideoClip && !root.analysed && !EditorState.scenes.detecting
        text: qsTr("Find scenes first to see where the clip would be cut.")
    }
}
