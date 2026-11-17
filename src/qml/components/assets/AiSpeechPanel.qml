import QtQuick
import QtQuick.Controls.Basic
import QtMultimedia
import Drift

// Dafin: AI › Text to speech. Chatterbox on this computer (EditorState.tts, voices from
// EditorState.voices): type, pick a voice, listen to a preview, then Insert puts the voice-over at
// the playhead on the first audio track with room (one undo step). Insert reuses the previewed
// take when nothing changed since, so it is not spoken twice. Cloning a voice stays in the Speech
// tab of the media panel.
AiToolPage {
    id: root

    description: qsTr("Type a script and turn it into a natural voice-over. Runs on this computer.")

    readonly property bool ready: EditorState.tts.available
    readonly property bool busy: EditorState.tts.running || EditorState.voices.encoding
    readonly property bool hasText: script.text.trim().length > 0
    property string previewUrl: ""
    // The settings the preview was made with; editing any of them makes it stale.
    property string previewKey: ""
    readonly property string currentKey: script.text.trim() + "|" + (voiceBox.currentValue || "")
                                         + "|" + (langBox.currentValue || "")
    readonly property bool previewFresh: root.previewUrl.length > 0 && root.previewKey === root.currentKey
    property string resultText: ""
    property bool resultOk: true

    Component.onCompleted: {
        if (root.ready)
            EditorState.tts.warmUp()
    }
    Component.onDestruction: player.stop()

    Connections {
        target: EditorState.tts
        function onAvailableChanged() {
            if (EditorState.tts.available)
                EditorState.tts.warmUp()
        }
        function onPreviewReady(url) {
            root.previewUrl = url
            player.source = url
            player.play()
        }
        function onFinished(ok, message) {
            root.resultOk = ok
            root.resultText = message
        }
    }

    MediaPlayer {
        id: player
        audioOutput: AudioOutput {}
    }

    AiModelDownload {
        width: root.contentWidth
        visible: !root.ready
        addonIds: ["tts.chatterbox-multilingual"]
        purpose: qsTr("Text to speech needs the voice model.")
        buttonText: qsTr("Download voice model")
    }

    Column {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.ready

        ThemedTextArea {
            id: script
            width: root.contentWidth
            height: 120
            enabled: !EditorState.tts.running
            placeholderText: qsTr("Type what the voice should say")
            placeholderTextColor: Theme.c.subtitlesTab.foregroundMuted
        }

        AiHint {
            readonly property int seconds: Math.ceil(script.text.length / 14)
            warning: seconds > 30
            text: seconds > 30
                  ? qsTr("About %1 s. Speech longer than 30 seconds often ends in gibberish; split the script into shorter parts.").arg(seconds)
                  : qsTr("%1 characters, about %2 s").arg(script.text.length).arg(seconds)
        }

        Text {
            text: qsTr("Voice")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: voiceBox
            width: root.contentWidth
            enabled: !root.busy
            model: EditorState.voices
            textRole: "name"
            valueRole: "id"
            Component.onCompleted: {
                const row = EditorState.voices.indexOfId(EditorState.tts.lastVoice)
                currentIndex = row >= 0 ? row : 0
            }
        }

        Text {
            text: qsTr("Language")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        ThemedComboBox {
            id: langBox
            width: root.contentWidth
            enabled: !EditorState.tts.running
            textRole: "label"
            valueRole: "code"
            model: EditorState.tts.languageOptions()
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(EditorState.tts.lastLanguage))
        }

        AiHint {
            text: qsTr("New voices (record or clone one) are made in the Speech tab of the media panel.")
        }
    }

    Row {
        width: root.contentWidth
        spacing: Theme.spacingSm
        visible: root.ready && !EditorState.tts.running

        ThemedButton {
            id: previewButton
            readonly property bool playing: player.playbackState === MediaPlayer.PlayingState
            width: (root.contentWidth - parent.spacing) / 2
            text: playing ? qsTr("Stop") : (root.previewFresh ? qsTr("Play again") : qsTr("Preview"))
            variant: "secondary"
            glyph: playing ? Theme.icons.pause : Theme.icons.play
            enabled: root.hasText && !root.busy
            tooltip: qsTr("Hear the voice-over before it goes on the timeline")
            onClicked: {
                if (playing) {
                    player.stop()
                } else if (root.previewFresh) {
                    player.play()
                } else {
                    root.resultText = ""
                    root.previewKey = root.currentKey
                    root.previewUrl = ""
                    EditorState.tts.preview(script.text, voiceBox.currentValue, langBox.currentValue,
                                            EditorState.tts.lastExaggeration)
                }
            }
        }

        ThemedButton {
            width: (root.contentWidth - parent.spacing) / 2
            text: qsTr("Insert")
            variant: "primary"
            glyph: Theme.icons.plus
            enabled: root.hasText && !root.busy
            tooltip: qsTr("Put the voice-over at the playhead, on the first audio track with room")
            onClicked: {
                player.stop()
                root.resultText = ""
                EditorState.tts.insertAtPlayhead(script.text, voiceBox.currentValue, langBox.currentValue,
                                                 EditorState.tts.lastExaggeration)
                // The previewed take, if used, now belongs to the project.
                root.previewUrl = ""
                root.previewKey = ""
            }
        }
    }

    AiHint {
        visible: root.ready && !root.hasText && !EditorState.tts.running
        text: qsTr("Type a script to preview or insert it.")
    }

    AiHint {
        visible: EditorState.tts.loading && !EditorState.tts.running
        text: qsTr("Loading voice model (first use takes a while)…")
    }

    AiJobStatus {
        running: EditorState.tts.running
        progress: EditorState.tts.progress
        status: EditorState.tts.loading ? qsTr("Loading voice model…") : EditorState.tts.status
        cancelTooltip: qsTr("Stop generating speech")
        onCancelRequested: EditorState.tts.cancel()
    }

    AiHint {
        visible: root.resultText.length > 0 && !EditorState.tts.running
        warning: !root.resultOk
        text: root.resultText
    }
}
