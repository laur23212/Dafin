import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › AI audio denoise. DeepFilterNet removes background noise from the selected clip's
// sound (EditorState.denoise.applyInPlace) and the clip is pointed at the cleaned file in place:
// its timing, volume dots, fades and effects stay, the same as Audio tools › Noise reduction, and
// one undo brings the original back. The full window keeps its A/B preview.
AiToolPage {
    id: root

    description: qsTr("Cleans background noise (fans, traffic, hum) out of voices in the selected clip. Runs on this computer.")

    property bool ready: Addons.hasKind("denoise-model") && Addons.runtimeAvailable()
    property string resultText: ""
    property bool resultOk: true

    readonly property var clip: EditorState.selectedClipData
    readonly property bool canClean: {
        void root.clip
        return EditorState.denoise.canApplyInPlace(EditorState.selectedTrack, EditorState.selectedClip)
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "denoise-model" || kind === "onnxruntime")
                root.ready = Addons.hasKind("denoise-model") && Addons.runtimeAvailable()
        }
    }

    Connections {
        target: EditorState.denoise
        function onFinished(ok, message) {
            root.resultOk = ok
            root.resultText = message
        }
    }

    AiModelDownload {
        width: root.contentWidth
        visible: !root.ready
        addonIds: ["deepfilternet.v3"]
        purpose: qsTr("AI audio denoise needs the noise removal model.")
        buttonText: qsTr("Download noise removal")
    }

    AiHint {
        visible: root.ready && !root.canClean
        text: qsTr("Select a video or audio clip with sound on the timeline first.")
    }

    Column {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.ready && root.canClean && !EditorState.denoise.running

        Text {
            text: qsTr("Strength")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedSlider {
            id: strength
            width: root.contentWidth
            label: qsTr("Noise removal strength")
            from: 0.1
            to: 1
            value: LayoutMemory.layoutNumber("ai/denoise/strength", 1)
            defaultValue: 1
            valueFormatter: function (v) { return Math.round(v * 100) + "%" }
            onMoved: LayoutMemory.setLayoutNumber("ai/denoise/strength", value)
        }

        AiHint {
            text: qsTr("Lower keeps some of the room sound, which can sound more natural.")
        }

        ThemedButton {
            width: root.contentWidth
            text: qsTr("Remove noise")
            variant: "primary"
            glyph: Theme.icons.headphones
            tooltip: qsTr("Clean the clip's sound in place. Its timing, volume dots, fades and effects stay; Undo brings the original back.")
            onClicked: {
                root.resultText = ""
                EditorState.denoise.applyInPlace(EditorState.selectedTrack, EditorState.selectedClip, strength.value)
            }
        }
    }

    AiJobStatus {
        running: EditorState.denoise.running
        progress: EditorState.denoise.progress
        status: EditorState.denoise.status
        cancelTooltip: qsTr("Stop removing noise")
        onCancelRequested: EditorState.denoise.cancel()
    }

    AiHint {
        visible: root.resultText.length > 0 && !EditorState.denoise.running
        warning: !root.resultOk
        text: root.resultText
    }

    ThemedButton {
        visible: root.ready && root.canClean && !EditorState.denoise.running
        text: qsTr("Open full window")
        variant: "secondary"
        glyph: Theme.icons.externalLink
        tooltip: qsTr("Compare the original and the cleaned sound before committing. The window adds the result on a new track.")
        onClicked: ShellBridge.main.openDenoise(EditorState.selectedTrack, EditorState.selectedClip,
                                                root.clip.duration !== undefined ? root.clip.duration : 0)
    }
}
