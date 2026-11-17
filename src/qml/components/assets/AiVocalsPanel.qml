import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Vocal remover. Splits the sound of the selected clip into the voice and the music
// with HTDemucs, on this computer (EditorState.vocals). Keep the vocals, the music, or both on two
// linked tracks; one undo step either way. The first use downloads the model (and the AI engine if
// there is none yet); Settings › AI & Cloud › Downloaded AI models deletes it again.
Item {
    id: root

    property bool modelInstalled: Addons.hasKind("separation-model")
    property bool runtimeReady: Addons.runtimeAvailable()
    readonly property bool ready: root.modelInstalled && root.runtimeReady

    // What still has to be downloaded: the model, or only the AI engine it runs on when the
    // engine was deleted and the model kept.
    readonly property var neededIds: {
        void Addons.catalog
        if (!root.modelInstalled)
            return ["htdemucs.v4"]
        const requires = Addons.entry("htdemucs.v4").requires || []
        for (let i = 0; i < requires.length; ++i) {
            if (!requires[i].installed && requires[i].id)
                return [requires[i].id]
        }
        return []
    }

    // 0 vocals only, 1 music only, 2 both (VocalRemoverController::Output). Remembered.
    property int output: LayoutMemory.layoutNumber("ai/vocals/output", 2)
    readonly property var outputs: [
        { value: 0, glyph: Theme.icons.micVocal, label: qsTr("Vocals only"),
          hint: qsTr("The clip keeps only the voice. Good for a cappella or clean dialogue.") },
        { value: 1, glyph: Theme.icons.music, label: qsTr("Music only"),
          hint: qsTr("The clip keeps everything but the voice: karaoke or an instrumental.") },
        { value: 2, glyph: Theme.icons.layers, label: qsTr("Both"),
          hint: qsTr("The voice and the music on two linked audio tracks under the clip, in sync, to mix as you like.") }
    ]

    function outputRow(value) {
        for (let i = 0; i < root.outputs.length; ++i) {
            if (root.outputs[i].value === value)
                return root.outputs[i]
        }
        return root.outputs[2]
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "separation-model" && kind !== "onnxruntime" && kind !== "onnxruntime-ep")
                return
            root.modelInstalled = Addons.hasKind("separation-model")
            root.runtimeReady = Addons.runtimeAvailable()
        }
        function onCatalogChanged() {
            root.modelInstalled = Addons.hasKind("separation-model")
            root.runtimeReady = Addons.runtimeAvailable()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: controls.height + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: controls
            x: Theme.pagePadding
            width: flick.width - Theme.pagePadding * 2
            spacing: Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingMd

            Text {
                width: controls.width
                wrapMode: Text.WordWrap
                text: qsTr("Splits the sound of the selected clip into the voice and the music. Runs on this computer; nothing is uploaded.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // --- first use: download the model (and the AI engine) ----------------------
            AiModelDownload {
                width: controls.width
                visible: !root.ready
                addonIds: root.neededIds
                purpose: root.modelInstalled ? qsTr("The vocal remover runs on the AI engine.")
                                             : qsTr("The vocal remover needs its separation model (HTDemucs, MIT licence).")
                buttonText: root.modelInstalled ? qsTr("Download the AI engine") : qsTr("Download vocal remover")
            }

            // --- what to keep -----------------------------------------------------------
            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: root.ready

                Text {
                    text: qsTr("Keep")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Flow {
                    width: controls.width
                    spacing: Theme.spacingSm

                    Repeater {
                        model: root.outputs
                        delegate: ThemedChip {
                            required property var modelData
                            text: modelData.label
                            selected: root.output === modelData.value
                            enabled: !EditorState.vocals.running
                            tooltip: modelData.hint
                            onClicked: {
                                root.output = modelData.value
                                LayoutMemory.setLayoutNumber("ai/vocals/output", modelData.value)
                            }
                        }
                    }
                }

                Text {
                    width: controls.width
                    wrapMode: Text.WordWrap
                    text: root.outputRow(root.output).hint
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // --- the clip, the button and the run ------------------------------------------
            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: root.ready

                Text {
                    width: controls.width
                    wrapMode: Text.WordWrap
                    visible: !EditorState.vocals.running
                    text: EditorState.vocals.canSeparate
                          ? qsTr("Clip: %1").arg(EditorState.vocals.clipName)
                          : qsTr("Select a clip with sound on the timeline first.")
                    color: EditorState.vocals.canSeparate ? Theme.c.subtitlesTab.foreground
                                                          : Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    elide: Text.ElideRight
                    maximumLineCount: 2
                }

                ThemedButton {
                    width: controls.width
                    visible: !EditorState.vocals.running
                    text: qsTr("Separate")
                    variant: "primary"
                    glyph: root.outputRow(root.output).glyph
                    enabled: EditorState.vocals.canSeparate
                    tooltip: EditorState.vocals.canSeparate
                             ? qsTr("Split the selected clip's sound. One undo step.")
                             : qsTr("Select a clip with sound first")
                    onClicked: EditorState.vocals.separate(root.output)
                }

                Text {
                    width: controls.width
                    visible: EditorState.vocals.running
                    elide: Text.ElideRight
                    text: qsTr("%1 %2%").arg(EditorState.vocals.status).arg(Math.round(EditorState.vocals.progress * 100))
                    color: Theme.c.subtitlesTab.statusForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedProgressBar {
                    width: controls.width
                    visible: EditorState.vocals.running
                    value: EditorState.vocals.progress
                }

                ThemedButton {
                    visible: EditorState.vocals.running
                    text: qsTr("Cancel")
                    variant: "destructive"
                    glyph: Theme.icons.x
                    tooltip: qsTr("Stop the vocal remover. Nothing is changed.")
                    onClicked: EditorState.vocals.cancel()
                }

                Text {
                    width: controls.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Long clips take a while: the audio is worked through 7.8 seconds at a time. A graphics card set up in Settings › AI & Cloud makes it much faster.")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }
}
