import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Smart short clips (EditorState.ai.shorts). Finds highlights of 15–60 s in the
// selected video clip — whole sentences from its transcript (made first when there is none), its
// speech energy, its shot changes and keywords — and lists them with their score; a click plays
// one. Create turns each ticked one into a 9:16 edition: a project of its own next to this one,
// tied to it by the edition link, reframed to follow faces (centre crop when there are none),
// with captions when asked for. Everything runs on this computer.
Item {
    id: root

    readonly property var shorts: EditorState.ai.shorts
    property bool whisperReady: Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
    property bool faceReady: Addons.hasKind("face-model") && Addons.runtimeAvailable()

    Connections {
        target: Addons
        function onKindChanged(kind) {
            root.whisperReady = Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
            root.faceReady = Addons.hasKind("face-model") && Addons.runtimeAvailable()
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        contentWidth: width
        contentHeight: column.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar { }

        Column {
            id: column
            width: flick.width
            spacing: Theme.spacingLg
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingLg

            Text {
                width: column.width
                wrapMode: Text.WordWrap
                text: qsTr("Finds the best 15–60 second moments in the selected clip, ends them on whole sentences, and turns the ones you tick into 9:16 shorts that follow the faces. Runs on this computer.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // --- optional models -----------------------------------------------------------
            AiModelDownload {
                id: speechDownload
                width: column.width
                compact: true
                visible: !root.whisperReady && (speechDownload.known || speechDownload.busy)
                addonIds: ["whisper.small", "silero.vad"]
                purpose: qsTr("Recommended: speech recognition, so shorts start and end on whole sentences.")
            }
            AiModelDownload {
                id: faceDownload
                width: column.width
                compact: true
                visible: !root.faceReady && (faceDownload.known || faceDownload.busy)
                addonIds: ["face.landmark"]
                purpose: qsTr("Optional: face tracking, so the 9:16 crop follows the speaker.")
            }

            // --- source and settings ---------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingSm

                Text {
                    width: column.width
                    elide: Text.ElideMiddle
                    text: root.shorts.canAnalyze ? qsTr("Clip: %1").arg(root.shorts.sourceName) : root.shorts.sourceHint
                    color: root.shorts.canAnalyze ? Theme.c.subtitlesTab.foreground : Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: root.shorts.canAnalyze ? Font.DemiBold : Font.Normal
                }

                Text {
                    text: qsTr("Length: about %1 seconds").arg(root.shorts.targetSeconds)
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedSlider {
                    id: lengthSlider
                    width: column.width
                    from: 15
                    to: 60
                    stepSize: 5
                    snapMode: Slider.SnapAlways
                    value: root.shorts.targetSeconds
                    enabled: !root.shorts.busy
                    valueFormatter: function (v) { return qsTr("%1 s").arg(Math.round(v)) }
                    Accessible.name: qsTr("Length of the shorts")
                    onMoved: root.shorts.targetSeconds = Math.round(value)
                }

                ThemedTextField {
                    id: keywordsField
                    width: column.width
                    enabled: !root.shorts.busy
                    text: root.shorts.keywords
                    placeholderText: qsTr("Extra keywords, comma separated")
                    font.family: Theme.fontFamily
                    Accessible.name: qsTr("Extra keywords")
                    onEditingFinished: root.shorts.keywords = text
                }
            }

            // --- find ------------------------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingSm

                ThemedButton {
                    width: column.width
                    visible: !root.shorts.busy
                    text: qsTr("Find highlights")
                    variant: "primary"
                    glyph: Theme.icons.sparkles
                    enabled: root.shorts.canAnalyze
                    tooltip: root.shorts.canAnalyze
                             ? (root.whisperReady
                                ? qsTr("Transcribe the clip if needed, then score its sentences")
                                : qsTr("Without speech recognition the highlights follow the shots"))
                             : root.shorts.sourceHint
                    onClicked: {
                        keywordsField.focus = false
                        root.shorts.keywords = keywordsField.text
                        root.shorts.analyze(LayoutMemory.layoutString("ai/captions/language", ""))
                    }
                }

                Text {
                    width: column.width
                    visible: root.shorts.busy || root.shorts.status.length > 0
                    wrapMode: Text.WordWrap
                    text: root.shorts.busy && root.shorts.status.length === 0
                          ? qsTr("Working… %1%").arg(Math.round(root.shorts.progress * 100))
                          : root.shorts.status
                    color: Theme.c.subtitlesTab.statusForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedProgressBar {
                    width: column.width
                    visible: root.shorts.busy
                    value: root.shorts.progress
                }

                ThemedButton {
                    visible: root.shorts.busy
                    text: qsTr("Cancel")
                    variant: "destructive"
                    glyph: Theme.icons.x
                    onClicked: root.shorts.cancel()
                }
            }

            // --- the highlights ------------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingXs
                visible: root.shorts.segments.length > 0

                Item {
                    width: column.width
                    height: Theme.controlHeightSm

                    Text {
                        anchors.left: parent.left
                        anchors.right: selectButtons.left
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                        text: root.shorts.resultFromSpeech
                              ? qsTr("%n highlight(s) in %1", "", root.shorts.segments.length).arg(root.shorts.resultName)
                              : qsTr("%n highlight(s) by shots in %1", "", root.shorts.segments.length).arg(root.shorts.resultName)
                        color: Theme.c.subtitlesTab.foreground
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontSizeXs
                        font.weight: Font.DemiBold
                    }

                    Row {
                        id: selectButtons
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacingXs
                        ThemedButton {
                            text: qsTr("All")
                            variant: "ghost"
                            enabled: !root.shorts.busy
                            onClicked: root.shorts.setAllChecked(true)
                        }
                        ThemedButton {
                            text: qsTr("None")
                            variant: "ghost"
                            enabled: !root.shorts.busy
                            onClicked: root.shorts.setAllChecked(false)
                        }
                    }
                }

                Repeater {
                    model: root.shorts.segments
                    delegate: AiShortRow {
                        width: column.width
                    }
                }

                ThemedCheckBox {
                    text: qsTr("Add captions")
                    checked: root.shorts.captions
                    enabled: !root.shorts.busy && root.shorts.resultFromSpeech
                    tooltip: root.shorts.resultFromSpeech ? qsTr("Captions from the transcript, a few words at a time")
                                                          : qsTr("Captions need speech")
                    onToggled: root.shorts.captions = checked
                }

                ThemedButton {
                    width: column.width
                    text: qsTr("Create %n short(s) (9:16)", "", root.shorts.checkedCount)
                    variant: "primary"
                    glyph: Theme.icons.smartphone
                    enabled: root.shorts.checkedCount > 0 && !root.shorts.busy
                    tooltip: qsTr("Each short is saved next to this project as a project of its own, linked to it as another edition")
                    onClicked: root.shorts.create()
                }

                Text {
                    width: column.width
                    wrapMode: Text.WordWrap
                    text: root.faceReady
                          ? qsTr("Each short is its own project next to this one, linked to it. The crop follows the faces.")
                          : qsTr("Each short is its own project next to this one, linked to it. Without face tracking the crop stays in the middle.")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }

            // --- what Create wrote ---------------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingXs
                visible: root.shorts.created.length > 0

                Text {
                    text: qsTr("Created")
                    color: Theme.c.subtitlesTab.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.DemiBold
                }

                Repeater {
                    model: root.shorts.created
                    delegate: Item {
                        required property var modelData
                        width: column.width
                        height: Theme.controlHeight

                        Text {
                            anchors.left: parent.left
                            anchors.right: openButton.left
                            anchors.rightMargin: Theme.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            elide: Text.ElideMiddle
                            text: qsTr("%1 (%2)").arg(modelData.name).arg(modelData.length)
                            color: Theme.c.subtitlesTab.foreground
                            font.family: Theme.fontFamily
                            font.pixelSize: Theme.fontSizeXs
                        }
                        ThemedButton {
                            id: openButton
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Open")
                            variant: "secondary"
                            tooltip: modelData.path
                            // Asks about unsaved changes first, like opening a recent project.
                            onClicked: ShellBridge.main.requestOpenRecentProject(modelData.path)
                        }
                    }
                }
            }
        }
    }
}
