import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: the AI category of the assets panel. Every tool here runs on this computer, with no
// cloud and no account or API key; each downloads its model the first time it is used, and
// Settings › AI & Cloud › Downloaded AI models deletes them again. The grid shows every tool, the
// ones not built yet greyed out; picking one opens it in place, with a way back to the grid.
Item {
    id: root

    // The open tool's id, or "" for the grid. AssetsPanel keeps it across tab switches.
    property string openTool: ""

    readonly property var tools: [
        { id: "captions", icon: "captions", available: true,
          title: qsTr("Auto captions"),
          description: qsTr("Speech to text: captions on the timeline and a transcript you can correct.") },
        { id: "textEdit", icon: "file-text", available: true,
          title: qsTr("Text-based editing"),
          description: qsTr("Delete words in the transcript to cut them from the video.") },
        { id: "silence", icon: "audio-waveform", available: true,
          title: qsTr("Silence & filler words"),
          description: qsTr("Remove pauses and filler words like “um” and “uh”.") },
        { id: "sceneCut", icon: "scissors", available: true,
          title: qsTr("Smart scene cut"),
          description: qsTr("Split a video wherever the shot changes.") },
        { id: "denoise", icon: "headphones", available: true,
          title: qsTr("AI audio denoise"),
          description: qsTr("Clean background noise out of voices.") },
        { id: "vocals", icon: "mic-off", available: false,
          title: qsTr("Vocal remover"),
          description: qsTr("Split a song into the voice and the music.") },
        { id: "tts", icon: "megaphone", available: true,
          title: qsTr("Text to speech"),
          description: qsTr("Turn typed text into a natural voice-over.") },
        { id: "cutout", icon: "circle-dashed", available: true,
          title: qsTr("Background removal"),
          description: qsTr("Cut a person or object out of its background.") },
        { id: "enhance", icon: "zoom-in", available: true,
          title: qsTr("Video enhancer"),
          description: qsTr("Upscale and sharpen low-resolution video.") },
        { id: "shorts", icon: "smartphone", available: true,
          title: qsTr("Smart short clips"),
          description: qsTr("Find the highlights and reframe them to 9:16.") },
        { id: "tagging", icon: "tags", available: true,
          title: qsTr("Media auto-tagging"),
          description: qsTr("Tag your media so the bin can be searched by what is in it.") }
    ]

    function toolById(id) {
        for (let i = 0; i < root.tools.length; ++i) {
            if (root.tools[i].id === id)
                return root.tools[i]
        }
        return null
    }

    // Dafin: low-spec (compatibility) mode turns every local AI tool off for this run.
    readonly property bool lowSpecOff: EditorState.preferences.lowSpecMode

    // A remembered tool that is not available (any more) shows the grid instead.
    readonly property var currentTool: {
        const tool = root.openTool.length > 0 ? root.toolById(root.openTool) : null
        return tool && tool.available && !root.lowSpecOff ? tool : null
    }

    Flickable {
        id: grid
        anchors.fill: parent
        visible: !root.currentTool
        contentWidth: width
        contentHeight: gridColumn.height + Theme.spacing3xl
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: gridColumn
            x: Theme.pagePadding
            width: grid.width - Theme.pagePadding * 2
            topPadding: Theme.pagePadding
            spacing: Theme.spacingLg

            Text {
                width: gridColumn.width
                wrapMode: Text.WordWrap
                text: qsTr("AI tools that run on this computer. Nothing is uploaded and no account is needed. Each tool downloads what it needs the first time you use it.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Text {
                visible: root.lowSpecOff
                width: gridColumn.width
                wrapMode: Text.WordWrap
                text: qsTr("Compatibility mode: this computer is below Dafin's recommended specs, so local AI is off. Settings › Performance › Low-spec mode turns it back on.")
                color: Theme.c.subtitlesTab.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            Flow {
                id: cards
                width: gridColumn.width
                spacing: Theme.spacingMd

                readonly property real minCardWidth: 150
                readonly property int columns: Math.max(1, Math.floor((width + spacing) / (minCardWidth + spacing)))
                readonly property real cardWidth: Math.floor((width - spacing * (columns - 1)) / columns)

                Repeater {
                    model: root.tools
                    delegate: AiToolCard {
                        required property var modelData
                        tool: modelData
                        lowSpecOff: root.lowSpecOff && modelData.available === true
                        width: cards.cardWidth
                        onOpenRequested: root.openTool = modelData.id
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        visible: !!root.currentTool

        // Back to all tools, and the open tool's name.
        Item {
            id: toolHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Theme.pagePadding - Theme.spacingSm
            anchors.rightMargin: Theme.pagePadding
            anchors.topMargin: Theme.spacingSm
            height: Theme.controlHeight

            IconButton {
                id: backButton
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.chevronLeft
                tooltip: qsTr("All AI tools")
                Accessible.name: qsTr("All AI tools")
                onClicked: root.openTool = ""
            }

            IconGlyph {
                id: headerIcon
                anchors.left: backButton.right
                anchors.leftMargin: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                glyph: root.currentTool ? root.currentTool.icon : ""
                iconSize: 18
                iconColor: Theme.c.subtitlesTab.accent
            }

            Text {
                anchors.left: headerIcon.right
                anchors.leftMargin: Theme.spacingSm
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.currentTool ? root.currentTool.title : ""
                color: Theme.c.subtitlesTab.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }
        }

        Loader {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolHeader.bottom
            anchors.bottom: parent.bottom
            anchors.topMargin: Theme.spacingXs
            sourceComponent: {
                switch (root.currentTool ? root.currentTool.id : "") {
                case "captions": return captionsTool
                case "textEdit": return textEditTool
                case "silence": return silenceTool
                case "sceneCut": return sceneCutTool
                case "denoise": return denoiseTool
                case "tts": return speechTool
                case "cutout": return cutoutTool
                case "enhance": return enhanceTool
                case "shorts": return shortsTool
                case "tagging": return taggingTool
                }
                return null
            }
        }
    }

    Component {
        id: captionsTool
        AiCaptionsPanel { }
    }
    Component {
        id: sceneCutTool
        AiSceneCutPanel { }
    }
    Component {
        id: denoiseTool
        AiDenoisePanel { }
    }
    Component {
        id: speechTool
        AiSpeechPanel { }
    }
    Component {
        id: cutoutTool
        AiCutoutPanel { }
    }
    Component {
        id: enhanceTool
        AiEnhancePanel { }
    }

    Component {
        id: textEditTool
        AiTextEditPanel { }
    }

    Component {
        id: silenceTool
        AiCleanupPanel { }
    }

    Component {
        id: shortsTool
        AiShortsPanel { }
    }

    Component {
        id: taggingTool
        AiTaggingPanel { }
    }
}
