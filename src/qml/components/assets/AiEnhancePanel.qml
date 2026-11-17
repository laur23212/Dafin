import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Video enhancer. Upscales and cleans up the selected video clip's used range with the
// restore models (EditorState.restore.run, a JobRegistry job): the result is added to the media
// bin beside the original, which is left as it is. The full Enhance window has the before/after
// preview, time estimates and custom models.
AiToolPage {
    id: root

    description: qsTr("Upscales low-resolution video and cleans up blocky compression in the selected clip. The enhanced copy is added to the media bin; the original stays as it is. Runs on this computer.")

    property var models: EditorState.restore.models()
    readonly property var upscaleOptions: {
        const out = [{ label: qsTr("Keep the size"), value: "" }]
        for (let i = 0; i < root.models.length; ++i) {
            if (root.models[i].task === "upscale")
                out.push({ label: root.models[i].name, value: root.models[i].id })
        }
        return out
    }
    readonly property var cleanOptions: {
        const out = [{ label: qsTr("No clean-up"), value: "" }]
        for (let i = 0; i < root.models.length; ++i) {
            if (root.models[i].task === "decompress")
                out.push({ label: root.models[i].name, value: root.models[i].id })
        }
        return out
    }
    readonly property bool ready: Addons.runtimeAvailable() && root.models.length > 0

    readonly property var clip: EditorState.selectedClipData
    readonly property bool hasVideoClip: clip.kind === "video"
    readonly property string targetId: hasVideoClip && clip.id ? clip.id : ""

    property int jobRevision: 0
    readonly property var job: { void root.jobRevision; return root.targetId ? EditorState.restore.job(root.targetId) : ({}) }
    readonly property bool running: root.job.active === true
    readonly property bool hasChoice: (upscaleBox.currentValue || "") !== "" || (cleanBox.currentValue || "") !== ""

    Connections {
        target: EditorState.restore
        function onJobChanged(targetId) {
            if (targetId === root.targetId)
                root.jobRevision++
        }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "restore-model" || kind === "onnxruntime")
                root.models = EditorState.restore.models()
        }
    }

    AiModelDownload {
        width: root.contentWidth
        visible: !root.ready
        addonIds: ["restore.models"]
        purpose: qsTr("The video enhancer needs the upscaling models.")
        buttonText: qsTr("Download video enhancer")
    }

    AiHint {
        visible: !root.hasVideoClip
        text: qsTr("Select a video clip on the timeline first.")
    }

    Column {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.ready && root.hasVideoClip && !root.running

        Text {
            text: qsTr("Upscale")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: upscaleBox
            width: root.contentWidth
            model: root.upscaleOptions
            textRole: "label"
            valueRole: "value"
            currentIndex: root.upscaleOptions.length > 1 ? 1 : 0
        }

        Text {
            text: qsTr("Sharpen and clean up compression")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: cleanBox
            width: root.contentWidth
            model: root.cleanOptions
            textRole: "label"
            valueRole: "value"
        }

        ThemedButton {
            width: root.contentWidth
            text: qsTr("Enhance video")
            variant: "primary"
            glyph: Theme.icons.zoomIn
            enabled: root.hasChoice
            tooltip: root.hasChoice ? qsTr("Enhance the part of the clip that is on the timeline. Long clips take a while.")
                                    : qsTr("Pick an upscaler or a clean-up first")
            onClicked: EditorState.restore.run(root.targetId, cleanBox.currentValue || "", upscaleBox.currentValue || "")
        }

        AiHint {
            visible: !root.hasChoice
            text: qsTr("Pick an upscaler or a clean-up to enhance the clip.")
        }
    }

    AiJobStatus {
        running: root.running
        progress: Number(root.job.progress || 0)
        status: root.job.status || ""
        cancelTooltip: qsTr("Stop enhancing")
        onCancelRequested: EditorState.restore.cancel(root.targetId)
    }

    AiHint {
        visible: !root.running && !!root.job.error
        warning: true
        text: root.job.error || ""
    }

    ThemedButton {
        // The phone shell has no Enhance window.
        visible: root.hasVideoClip && !root.running && typeof ShellBridge.main?.openRestore === "function"
        text: qsTr("Open full window")
        variant: "secondary"
        glyph: Theme.icons.externalLink
        tooltip: qsTr("Compare before and after on one frame, see how long it will take, and use your own models")
        onClicked: ShellBridge.main.openRestore(EditorState.selectedTrack, EditorState.selectedClip)
    }
}
