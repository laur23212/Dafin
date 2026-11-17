import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Text-based editing. The transcript of the take in focus (EditorState.ai), word by
// word. Click a word to put the playhead on it, Shift-click or drag to select a stretch, click a
// line's time to select the whole line; Delete (or Cut) removes those words from the clip and its
// linked clips, closes the gaps with a 30 ms de-click and moves the captions made from them, as
// one undo step. Protected ranges are refused with a message. The word being said is lit. A clip
// without a transcript is transcribed first, on the captions' speech recognition.
Item {
    id: root

    property bool whisperReady: Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
    property var languages: EditorState.whisperLanguages()
    readonly property var info: EditorState.ai.transcriptInfo
    readonly property bool hasTranscript: !!root.info.hasTranscript

    // The flat word list (transcriptWords) and the selection in it: rows anchor..focus.
    property var words: EditorState.ai.transcriptWords
    property int anchorRow: -1
    property int focusRow: -1
    property int currentRow: -1
    readonly property int selFirst: Math.min(root.anchorRow, root.focusRow)
    readonly property int selLast: Math.max(root.anchorRow, root.focusRow)
    readonly property int selCount: root.anchorRow >= 0 && root.focusRow >= 0 ? root.selLast - root.selFirst + 1 : 0
    property string shownClipId: ""

    function indexOfValue(rows, key, value) {
        for (let i = 0; i < rows.length; ++i) {
            if (rows[i][key] === value)
                return i
        }
        return 0
    }

    function clearSelection() {
        root.anchorRow = -1
        root.focusRow = -1
    }

    function cutSelection() {
        if (root.selCount <= 0)
            return
        const list = []
        for (let r = root.selFirst; r <= root.selLast && r < root.words.length; ++r)
            list.push({ clip: root.words[r].clip, i: root.words[r].i })
        root.clearSelection()
        EditorState.ai.cutWords(list)
        lineList.forceActiveFocus()
    }

    // The word being said at `seconds`, or -1.
    function rowAt(seconds) {
        let lo = 0
        let hi = root.words.length - 1
        let found = -1
        while (lo <= hi) {
            const mid = (lo + hi) >> 1
            if (root.words[mid].start <= seconds) {
                found = mid
                lo = mid + 1
            } else {
                hi = mid - 1
            }
        }
        return found >= 0 && seconds < root.words[found].end ? found : -1
    }

    // Lines are updated in place when only their contents changed, so the list keeps its place.
    function sync() {
        const rows = EditorState.ai.transcriptLines
        const clipId = EditorState.ai.transcriptClipId
        const otherClip = clipId !== root.shownClipId
        root.shownClipId = clipId
        root.words = EditorState.ai.transcriptWords
        if (otherClip || rows.length !== lineModel.count) {
            const y = lineList.contentY
            lineModel.clear()
            for (let i = 0; i < rows.length; ++i)
                lineModel.append(rows[i])
            if (otherClip)
                lineList.positionViewAtBeginning()
            else
                lineList.contentY = Math.max(0, Math.min(y, lineList.contentHeight - lineList.height))
        } else {
            for (let j = 0; j < rows.length; ++j) {
                const a = rows[j]
                const b = lineModel.get(j)
                if (a.text !== b.text || a.start !== b.start || a.end !== b.end || a.time !== b.time
                        || a.speaker !== b.speaker || a.firstRow !== b.firstRow || a.rowCount !== b.rowCount)
                    lineModel.set(j, a)
            }
        }
        // The words under a selection moved; it is not carried over.
        root.clearSelection()
        root.currentRow = root.rowAt(EditorState.playheadSeconds)
    }

    Component.onCompleted: {
        EditorState.ai.transcriptVisible = true
        sync()
    }
    Component.onDestruction: EditorState.ai.transcriptVisible = false

    Connections {
        target: EditorState.ai
        function onTranscriptChanged() { root.sync() }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind !== "whisper-model" && kind !== "onnxruntime")
                return
            root.whisperReady = Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
            root.languages = EditorState.whisperLanguages()
        }
    }

    Connections {
        target: EditorState
        function onPlayheadSecondsChanged() {
            const row = root.rowAt(EditorState.playheadSeconds)
            if (row === root.currentRow)
                return
            root.currentRow = row
            if (row >= 0 && EditorState.playing && !lineList.moving && !picker.pressed)
                lineList.positionViewAtIndex(root.words[row].line, ListView.Contain)
        }
    }

    ListModel {
        id: lineModel
    }

    Column {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        spacing: Theme.spacingMd
        topPadding: Theme.spacingSm
        bottomPadding: Theme.spacingSm

        Text {
            width: controls.width
            wrapMode: Text.WordWrap
            text: qsTr("Edit the video by editing its words: select words in the transcript and press Delete to cut them out. Runs on this computer.")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Text {
            width: controls.width
            elide: Text.ElideRight
            text: root.info.clipName ? qsTr("Transcript · %1").arg(root.info.clipName) : qsTr("Transcript")
            color: Theme.c.subtitlesTab.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            font.weight: Font.Medium
        }

        Text {
            width: controls.width
            visible: !root.info.clipId
            wrapMode: Text.WordWrap
            text: qsTr("Select a video or audio clip on the timeline to edit what is said in it.")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        // --- no transcript yet: transcribe, on the captions' speech recognition ------------
        AiModelDownload {
            width: controls.width
            visible: !!root.info.clipId && !root.hasTranscript && !root.whisperReady
            addonIds: ["whisper.small", "silero.vad"]
            purpose: qsTr("Text-based editing needs speech recognition to make the transcript.")
            buttonText: qsTr("Download speech recognition")
        }

        Column {
            width: controls.width
            spacing: Theme.spacingSm
            visible: !!root.info.clipId && !root.hasTranscript && root.whisperReady

            Text {
                width: controls.width
                wrapMode: Text.WordWrap
                text: qsTr("No transcript for this clip yet.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedComboBox {
                id: languageBox
                width: controls.width
                enabled: !EditorState.ai.captioning
                textRole: "label"
                valueRole: "code"
                model: root.languages
                currentIndex: root.indexOfValue(root.languages, "code",
                                                LayoutMemory.layoutString("ai/captions/language", ""))
                onActivated: LayoutMemory.setLayoutString("ai/captions/language", currentValue || "")
            }

            ThemedButton {
                width: controls.width
                visible: !EditorState.ai.captioning
                text: qsTr("Transcribe")
                variant: "primary"
                glyph: Theme.icons.fileText
                enabled: EditorState.ai.canCaption && !EditorState.subtitleGenerating
                tooltip: qsTr("Make the transcript of the selected clip. No captions are added.")
                onClicked: EditorState.ai.transcribe(languageBox.currentValue || "")
            }

            Text {
                width: controls.width
                visible: EditorState.ai.captioning
                elide: Text.ElideRight
                text: EditorState.ai.captionStatus.length > 0
                      ? EditorState.ai.captionStatus
                      : qsTr("Transcribing… %1%").arg(Math.round(EditorState.ai.captionProgress * 100))
                color: Theme.c.subtitlesTab.statusForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedProgressBar {
                width: controls.width
                visible: EditorState.ai.captioning
                value: EditorState.ai.captionProgress
            }

            ThemedButton {
                visible: EditorState.ai.captioning
                text: qsTr("Cancel")
                variant: "destructive"
                glyph: Theme.icons.x
                onClicked: EditorState.ai.cancelCaptions()
            }
        }

        // --- the transcript's toolbar -------------------------------------------------------
        Text {
            width: controls.width
            visible: root.hasTranscript
            wrapMode: Text.WordWrap
            text: lineModel.count === 0
                  ? qsTr("No speech in this part of the clip.")
                  : qsTr("Click a word to jump to it, Shift-click or drag to select, click a time to select its line. Delete cuts the selection; captions follow. Undo puts it back.")
            color: Theme.c.subtitlesTab.foregroundMuted
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeXs
        }

        Row {
            width: controls.width
            spacing: Theme.spacingSm
            visible: root.hasTranscript && lineModel.count > 0

            ThemedButton {
                id: cutButton
                text: root.selCount > 0 ? qsTr("Cut %n word(s)", "", root.selCount) : qsTr("Cut")
                variant: "destructive"
                glyph: Theme.icons.scissors
                enabled: root.selCount > 0
                tooltip: qsTr("Cut the selected words from the clip (Delete)")
                onClicked: root.cutSelection()
            }

            ThemedButton {
                text: qsTr("Clear")
                enabled: root.selCount > 0
                tooltip: qsTr("Clear the selection (Esc)")
                onClicked: root.clearSelection()
            }
        }
    }

    ListView {
        id: lineList
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: controls.bottom
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        visible: root.hasTranscript
        clip: true
        spacing: 2
        model: lineModel
        boundsBehavior: Flickable.StopAtBounds
        activeFocusOnTab: true
        ScrollBar.vertical: AppScrollBar { id: lineScroll }
        Accessible.role: Accessible.List
        Accessible.name: qsTr("Transcript words")

        Keys.onDeletePressed: root.cutSelection()
        Keys.onPressed: (event) => {
            if (event.key === Qt.Key_Backspace) {
                root.cutSelection()
                event.accepted = true
            } else if (event.key === Qt.Key_Escape && root.selCount > 0) {
                root.clearSelection()
                event.accepted = true
            }
        }

        delegate: Item {
            id: line

            required property int index
            required property string time
            required property string speaker
            required property real start
            required property real end
            required property int firstRow
            required property int rowCount

            readonly property bool current: root.currentRow >= line.firstRow
                                            && root.currentRow < line.firstRow + line.rowCount

            // The transcript row under a point of this line, -1 for the time column, or -2.
            function rowAt(x, y) {
                if (x < body.x)
                    return -1
                const p = mapToItem(wordFlow, x, y)
                const hit = wordFlow.childAt(p.x, p.y)
                return hit && hit.row !== undefined ? hit.row : -2
            }

            width: lineList.width - lineScroll.width
            height: body.y + body.height + Theme.spacingSm

            Rectangle {
                anchors.fill: parent
                radius: Theme.c.scenesShotRow.radius
                color: Theme.c.subtitlesTab.accent
                opacity: line.current ? 0.08 : 0
            }

            Text {
                id: timeText
                x: Theme.spacingSm
                y: Theme.spacingSm
                width: timeMetrics.advanceWidth + Theme.spacingSm
                text: line.time
                color: line.current ? Theme.c.subtitlesTab.accent : Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            TextMetrics {
                id: timeMetrics
                font: timeText.font
                text: "0:00:00"
            }

            Column {
                id: body
                anchors.left: timeText.right
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                y: Theme.spacingSm
                spacing: 2

                Text {
                    width: parent.width
                    visible: line.speaker.length > 0
                    text: line.speaker
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Flow {
                    id: wordFlow
                    width: parent.width
                    spacing: 0

                    Repeater {
                        model: line.rowCount
                        delegate: Rectangle {
                            id: wordChip
                            required property int index
                            readonly property int row: line.firstRow + index
                            readonly property var word: root.words[row]
                            readonly property bool selected: root.selCount > 0 && row >= root.selFirst
                                                             && row <= root.selLast
                            readonly property bool said: root.currentRow === row

                            width: wordText.implicitWidth + 4
                            height: wordText.implicitHeight + 2
                            radius: 2
                            color: "transparent"
                            Accessible.role: Accessible.StaticText
                            Accessible.name: wordChip.word ? wordChip.word.text : ""
                            Accessible.selected: wordChip.selected

                            Rectangle {
                                anchors.fill: parent
                                radius: parent.radius
                                color: Theme.c.subtitlesTab.accent
                                opacity: wordChip.selected ? 1 : (wordChip.said ? 0.18 : 0)
                            }

                            Text {
                                id: wordText
                                x: 2
                                y: 1
                                text: wordChip.word ? wordChip.word.text : ""
                                color: wordChip.selected ? Theme.primaryForeground
                                                         : (wordChip.said ? Theme.c.subtitlesTab.accent
                                                                          : Theme.c.subtitlesTab.foreground)
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeSm
                                font.underline: wordChip.said && !wordChip.selected
                            }
                        }
                    }
                }
            }
        }
    }

    // One pointer handler over the words, so a drag can run across lines: press picks a word,
    // Shift extends from the anchor, dragging extends to the word under the pointer.
    MouseArea {
        id: picker
        anchors.fill: lineList
        anchors.rightMargin: Theme.pagePadding + lineScroll.width
        visible: lineList.visible
        preventStealing: true
        acceptedButtons: Qt.LeftButton
        cursorShape: Qt.IBeamCursor

        property bool dragged: false

        function hit(mx, my) {
            const p = mapToItem(lineList.contentItem, mx, my)
            const item = lineList.itemAt(p.x, p.y)
            if (!item)
                return { line: null, row: -2 }
            const q = mapToItem(item, mx, my)
            return { line: item, row: item.rowAt(q.x, q.y) }
        }

        onPressed: (mouse) => {
            lineList.forceActiveFocus()
            dragged = false
            const h = hit(mouse.x, mouse.y)
            if (!h.line)
                return
            if (h.row === -1) {
                // The time column: the whole line.
                root.anchorRow = h.line.firstRow
                root.focusRow = h.line.firstRow + h.line.rowCount - 1
                EditorState.ai.seekToWord(h.line.firstRow)
                return
            }
            if (h.row < 0)
                return
            if ((mouse.modifiers & Qt.ShiftModifier) && root.anchorRow >= 0) {
                root.focusRow = h.row
            } else {
                root.anchorRow = h.row
                root.focusRow = h.row
                EditorState.ai.seekToWord(h.row)
            }
        }
        onPositionChanged: (mouse) => {
            if (!pressed || root.anchorRow < 0)
                return
            const h = hit(mouse.x, Math.max(0, Math.min(height - 1, mouse.y)))
            if (h.row >= 0 && h.row !== root.focusRow) {
                dragged = true
                root.focusRow = h.row
            }
            // Past the top or bottom edge, the list scrolls on.
            if (mouse.y < 0)
                lineList.contentY = Math.max(0, lineList.contentY - 12)
            else if (mouse.y > height)
                lineList.contentY = Math.min(Math.max(0, lineList.contentHeight - lineList.height),
                                             lineList.contentY + 12)
        }
    }
}
