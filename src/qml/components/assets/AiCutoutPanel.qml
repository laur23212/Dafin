import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Background removal. People are cut out automatically with RVM
// (EditorState.segmentation.segmentClip, no clicks): the matte is pinned to the selected clip as a
// mask, one undo step. Picking any other subject by clicking it (SAM2) and refining frames is the
// full Cut out subject window.
AiToolPage {
    id: root

    description: qsTr("Removes the background behind people in the selected video clip, so you can put anything behind them. Runs on this computer.")

    property bool rvmReady: Addons.hasKind("rvm-model") && Addons.runtimeAvailable()
    property bool sam2Ready: Addons.hasKind("sam2-model") && Addons.runtimeAvailable()
    property var qualities: EditorState.segmentation.rvmQualities()
    property string resultText: ""
    property bool resultOk: true

    readonly property var clip: EditorState.selectedClipData
    readonly property bool hasVideoClip: clip.kind === "video"

    function refresh() {
        root.rvmReady = Addons.hasKind("rvm-model") && Addons.runtimeAvailable()
        root.sam2Ready = Addons.hasKind("sam2-model") && Addons.runtimeAvailable()
        root.qualities = EditorState.segmentation.rvmQualities()
    }

    function qualityLabel(variant) {
        if (variant === "resnet50")
            return qsTr("Best quality (slower)")
        if (variant === "mobilenetv3")
            return qsTr("Fast")
        return variant
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "rvm-model" || kind === "sam2-model" || kind === "onnxruntime")
                root.refresh()
        }
    }

    Connections {
        target: EditorState.segmentation
        function onFinished(ok, message) {
            root.resultOk = ok
            root.resultText = ok && message.length === 0 ? qsTr("Background removed") : message
        }
    }

    AiModelDownload {
        width: root.contentWidth
        visible: !root.rvmReady
        addonIds: ["rvm.mobilenetv3"]
        purpose: qsTr("Background removal needs the people cutout model.")
        buttonText: qsTr("Download background removal")
    }

    AiHint {
        visible: !root.hasVideoClip
        text: qsTr("Select a video clip on the timeline first.")
    }

    Column {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.rvmReady && root.hasVideoClip && !EditorState.segmentation.running

        Text {
            visible: root.qualities.length > 1
            text: qsTr("Quality")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: qualityBox
            visible: root.qualities.length > 1
            width: root.contentWidth
            model: root.qualities.map(v => ({ label: root.qualityLabel(v), value: v }))
            textRole: "label"
            valueRole: "value"
        }

        ThemedButton {
            width: root.contentWidth
            text: qsTr("Remove background")
            variant: "primary"
            glyph: Theme.icons.mask
            tooltip: qsTr("Find the people in the whole clip and hide everything else, as a mask on the clip. Undo takes it off.")
            onClicked: {
                root.resultText = ""
                EditorState.segmentation.segmentClip(EditorState.selectedTrack, EditorState.selectedClip,
                                                     [], "mask", "rvm",
                                                     root.qualities.length > 1 ? (qualityBox.currentValue || "") : "")
            }
        }
    }

    AiJobStatus {
        running: EditorState.segmentation.running
        progress: EditorState.segmentation.progress
        status: EditorState.segmentation.status
        cancelTooltip: qsTr("Stop removing the background")
        onCancelRequested: EditorState.segmentation.cancel()
    }

    AiHint {
        visible: root.resultText.length > 0 && !EditorState.segmentation.running
        warning: !root.resultOk
        text: root.resultText
    }

    // Anything that is not a person, or fine-tuning: the full window.
    ThemedButton {
        visible: root.hasVideoClip && (root.rvmReady || root.sam2Ready) && !EditorState.segmentation.running
        text: qsTr("Open full window")
        variant: "secondary"
        glyph: Theme.icons.externalLink
        tooltip: qsTr("Click the subject to cut out anything, not only people, and check it frame by frame")
        onClicked: ShellBridge.main.openSegmentation(EditorState.selectedTrack, EditorState.selectedClip,
                                                     root.clip.start !== undefined ? root.clip.start : 0,
                                                     root.clip.duration !== undefined ? root.clip.duration : 0)
    }

    AiModelDownload {
        id: sam2Download
        width: root.contentWidth
        compact: true
        visible: root.rvmReady && !root.sam2Ready && (sam2Download.known || sam2Download.busy)
        addonIds: ["sam2.tiny"]
        purpose: qsTr("Optional: cut out any object by clicking it, in the full window.")
    }
}
