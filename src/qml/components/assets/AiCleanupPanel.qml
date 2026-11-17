import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Silence & filler words. Finds the pauses (Silence remover's detection and settings)
// and the filler words (word timing in the transcript, from an editable per-language list kept in
// settings) of the take in focus (EditorState.ai). Each hit is listed with its time and a
// checkbox; click one to hear it. Remove cuts only the ticked ones, as one undo step, on the same
// path as Text-based editing: linked clips follow, gaps close, captions move, protected ranges
// are refused.
Item {
    id: root

    property bool whisperReady: Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
    readonly property var info: EditorState.ai.transcriptInfo
    readonly property bool hasTranscript: !!root.info.hasTranscript

    // The filler list being edited, for fillerLanguage.
    property string fillerLanguage: "en"
    property var fillerRows: []
    readonly property var fillerLanguages: {
        const rows = [{ code: "en", label: qsTr("English") }, { code: "ro", label: qsTr("Romanian") }]
        const lang = String(root.info.language || "").toLowerCase()
        if (lang.length > 0 && lang !== "en" && lang !== "ro")
            rows.push({ code: lang, label: lang.toUpperCase() })
        return rows
    }

    // One tick per hit; all ticked when found.
    property var ticks: []
    property real savedSeconds: 0
    readonly property int tickedCount: {
        let n = 0
        for (let i = 0; i < root.ticks.length; ++i)
            n += root.ticks[i] ? 1 : 0
        return n
    }

    function tickedRows() {
        const out = []
        for (let i = 0; i < root.ticks.length; ++i) {
            if (root.ticks[i])
                out.push(i)
        }
        return out
    }

    function setTick(row, on) {
        const next = root.ticks.slice()
        next[row] = on
        root.ticks = next
        root.savedSeconds = EditorState.ai.cleanupSeconds(root.tickedRows())
    }

    function tickAll(on) {
        const next = []
        for (let i = 0; i < EditorState.ai.cleanupHits.length; ++i)
            next.push(on)
        root.ticks = next
        root.savedSeconds = EditorState.ai.cleanupSeconds(root.tickedRows())
    }

    function loadFillers(lang) {
        root.fillerLanguage = lang
        root.fillerRows = EditorState.ai.fillerWords(lang)
    }

    function saveFillers(rows) {
        EditorState.ai.setFillerWords(root.fillerLanguage, rows)
        root.fillerRows = EditorState.ai.fillerWords(root.fillerLanguage)
    }

    function seconds(v) {
        return Number(v).toFixed(1) + " s"
    }

    Component.onCompleted: {
        EditorState.ai.transcriptVisible = true
        root.loadFillers(EditorState.ai.fillerLanguage())
        root.tickAll(true)
    }
    Component.onDestruction: {
        EditorState.ai.cancelCleanup()
        EditorState.ai.transcriptVisible = false
    }

    Connections {
        target: EditorState.ai
        function onCleanupChanged() { root.tickAll(true) }
    }

    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "whisper-model" || kind === "onnxruntime")
                root.whisperReady = Addons.hasKind("whisper-model") && Addons.runtimeAvailable()
        }
    }

    component Note: Text {
        width: parent ? parent.width : 0
        wrapMode: Text.WordWrap
        color: Theme.c.subtitlesTab.foregroundMuted
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    ListView {
        id: hitList
        anchors.fill: parent
        anchors.leftMargin: Theme.pagePadding
        anchors.rightMargin: Theme.pagePadding
        clip: true
        spacing: 2
        boundsBehavior: Flickable.StopAtBounds
        model: EditorState.ai.cleanupHits
        ScrollBar.vertical: AppScrollBar { id: hitScroll }

        header: Column {
            id: controls
            width: hitList.width - hitScroll.width
            spacing: Theme.spacingMd
            topPadding: Theme.spacingSm
            bottomPadding: Theme.spacingMd

            Note {
                text: qsTr("Finds the pauses and filler words in the selected clip and lists them, so you can choose which to cut. Runs on this computer.")
            }

            Text {
                width: controls.width
                elide: Text.ElideRight
                text: root.info.clipName ? qsTr("Clip · %1").arg(root.info.clipName)
                                         : qsTr("Select a video or audio clip on the timeline first.")
                color: root.info.clipName ? Theme.c.subtitlesTab.foreground : Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: root.info.clipName ? Theme.fontSizeSm : Theme.fontSizeXs
                font.weight: root.info.clipName ? Font.Medium : Font.Normal
            }

            // --- silences: Silence remover's settings ------------------------------------
            ThemedCheckBox {
                id: silenceBox
                width: controls.width
                text: qsTr("Silences")
                checked: LayoutMemory.layoutNumber("ai/cleanup/silences", 1) > 0
                onToggled: LayoutMemory.setLayoutNumber("ai/cleanup/silences", checked ? 1 : 0)
            }

            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: silenceBox.checked

                ThemedSlider {
                    id: silenceThreshold
                    width: parent.width
                    label: qsTr("Quieter than")
                    from: -70
                    to: -20
                    stepSize: 1
                    value: LayoutMemory.layoutNumber("ai/cleanup/threshold", -40)
                    valueFormatter: function (v) { return Math.round(v) + " dB" }
                    onMoved: LayoutMemory.setLayoutNumber("ai/cleanup/threshold", value)
                }
                ThemedSlider {
                    id: silenceMinLength
                    width: parent.width
                    label: qsTr("For at least")
                    from: 0.2
                    to: 3
                    stepSize: 0.1
                    value: LayoutMemory.layoutNumber("ai/cleanup/minLength", 0.6)
                    valueFormatter: function (v) { return Number(v).toFixed(1) + " s" }
                    onMoved: LayoutMemory.setLayoutNumber("ai/cleanup/minLength", value)
                }
                ThemedSlider {
                    id: silencePadding
                    width: parent.width
                    label: qsTr("Keep around speech")
                    from: 0
                    to: 0.5
                    stepSize: 0.01
                    value: LayoutMemory.layoutNumber("ai/cleanup/padding", 0.1)
                    valueFormatter: function (v) { return Number(v).toFixed(2) + " s" }
                    onMoved: LayoutMemory.setLayoutNumber("ai/cleanup/padding", value)
                }
            }

            // --- filler words: from the transcript, an editable list per language ---------
            ThemedCheckBox {
                id: fillerBox
                width: controls.width
                text: qsTr("Filler words")
                checked: LayoutMemory.layoutNumber("ai/cleanup/fillers", 1) > 0
                onToggled: LayoutMemory.setLayoutNumber("ai/cleanup/fillers", checked ? 1 : 0)
            }

            Column {
                width: controls.width
                spacing: Theme.spacingSm
                visible: fillerBox.checked

                Note {
                    visible: !!root.info.clipId && !root.hasTranscript
                    text: qsTr("Filler words are found in the transcript, and this clip has none yet.")
                }

                AiModelDownload {
                    width: parent.width
                    visible: !!root.info.clipId && !root.hasTranscript && !root.whisperReady
                    addonIds: ["whisper.small", "silero.vad"]
                    purpose: qsTr("Finding filler words needs speech recognition.")
                    buttonText: qsTr("Download speech recognition")
                }

                ThemedButton {
                    width: parent.width
                    visible: !!root.info.clipId && !root.hasTranscript && root.whisperReady
                             && !EditorState.ai.captioning
                    text: qsTr("Transcribe")
                    glyph: Theme.icons.fileText
                    enabled: EditorState.ai.canCaption && !EditorState.subtitleGenerating
                    tooltip: qsTr("Make the transcript of the selected clip. No captions are added.")
                    onClicked: EditorState.ai.transcribe(LayoutMemory.layoutString("ai/captions/language", ""))
                }

                Text {
                    width: parent.width
                    visible: EditorState.ai.captioning
                    elide: Text.ElideRight
                    text: EditorState.ai.captionStatus
                    color: Theme.c.subtitlesTab.statusForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                ThemedProgressBar {
                    width: parent.width
                    visible: EditorState.ai.captioning
                    value: EditorState.ai.captionProgress
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedComboBox {
                        id: fillerLangBox
                        width: parent.width - resetFillers.width - parent.spacing
                        textRole: "label"
                        valueRole: "code"
                        model: root.fillerLanguages
                        currentIndex: {
                            for (let i = 0; i < root.fillerLanguages.length; ++i) {
                                if (root.fillerLanguages[i].code === root.fillerLanguage)
                                    return i
                            }
                            return 0
                        }
                        onActivated: root.loadFillers(currentValue || "en")
                    }

                    IconButton {
                        id: resetFillers
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.icons.reset
                        tooltip: qsTr("Back to Dafin's list for this language")
                        Accessible.name: qsTr("Reset filler words")
                        onClicked: {
                            EditorState.ai.resetFillerWords(root.fillerLanguage)
                            root.loadFillers(root.fillerLanguage)
                        }
                    }
                }

                // Every entry with its own tick; phrases ("you know") match words in a row.
                Flow {
                    width: parent.width
                    spacing: Theme.spacingXs

                    Repeater {
                        model: root.fillerRows
                        delegate: Rectangle {
                            id: chip
                            required property var modelData
                            required property int index
                            width: chipRow.implicitWidth + Theme.spacingSm * 2
                            height: Theme.controlHeightSm
                            radius: height / 2
                            color: "transparent"
                            border.width: Theme.c.subtitlesTab.borderWidth
                            border.color: chip.modelData.enabled ? Theme.c.subtitlesTab.accent
                                                                 : Theme.c.subtitlesTab.border

                            Row {
                                id: chipRow
                                anchors.centerIn: parent
                                spacing: 2

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: chip.modelData.text
                                    color: chip.modelData.enabled ? Theme.c.subtitlesTab.foreground
                                                                  : Theme.c.subtitlesTab.foregroundMuted
                                    font.family: Theme.fontFamily
                                    font.pixelSize: Theme.fontSizeXs
                                    font.strikeout: !chip.modelData.enabled
                                }

                                IconButton {
                                    anchors.verticalCenter: parent.verticalCenter
                                    glyph: Theme.icons.x
                                    buttonSize: Theme.controlHeightSm - 6
                                    iconSize: Theme.iconSizeSm
                                    tooltip: qsTr("Remove from the list")
                                    Accessible.name: qsTr("Remove %1").arg(chip.modelData.text)
                                    onClicked: {
                                        const rows = root.fillerRows.slice()
                                        rows.splice(chip.index, 1)
                                        root.saveFillers(rows)
                                    }
                                }
                            }

                            TapHandler {
                                onTapped: {
                                    const rows = root.fillerRows.slice()
                                    rows[chip.index] = { text: chip.modelData.text, enabled: !chip.modelData.enabled }
                                    root.saveFillers(rows)
                                }
                            }
                            ThemedToolTip {
                                visible: chipHover.hovered
                                text: chip.modelData.enabled ? qsTr("Looked for. Click to skip it.")
                                                             : qsTr("Skipped. Click to look for it.")
                            }
                            HoverHandler {
                                id: chipHover
                                cursorShape: Qt.PointingHandCursor
                            }
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Theme.spacingSm

                    ThemedTextField {
                        id: newFiller
                        width: parent.width - addFiller.width - parent.spacing
                        placeholderText: qsTr("Add a word or phrase")
                        onAccepted: addFiller.clicked()
                    }

                    ThemedButton {
                        id: addFiller
                        text: qsTr("Add")
                        glyph: Theme.icons.plus
                        enabled: newFiller.text.trim().length > 0
                        onClicked: {
                            const text = newFiller.text.replace(/\s+/g, " ").trim()
                            if (text.length === 0)
                                return
                            const rows = root.fillerRows.slice()
                            rows.push({ text: text, enabled: true })
                            root.saveFillers(rows)
                            newFiller.text = ""
                        }
                    }
                }
            }

            // --- Find -------------------------------------------------------------------
            ThemedButton {
                width: controls.width
                visible: !EditorState.ai.cleanupBusy
                text: EditorState.ai.cleanupFound ? qsTr("Find again") : qsTr("Find")
                variant: "primary"
                glyph: Theme.icons.search
                enabled: !!root.info.clipId && (silenceBox.checked || fillerBox.checked)
                onClicked: EditorState.ai.findCleanup(silenceBox.checked, fillerBox.checked,
                                                      silenceThreshold.value, silenceMinLength.value,
                                                      silencePadding.value, root.fillerLanguage)
            }

            Text {
                width: controls.width
                visible: EditorState.ai.cleanupBusy
                elide: Text.ElideRight
                text: EditorState.ai.cleanupStatus
                color: Theme.c.subtitlesTab.statusForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            ThemedProgressBar {
                width: controls.width
                visible: EditorState.ai.cleanupBusy
                value: EditorState.audioTools.progress
            }

            ThemedButton {
                visible: EditorState.ai.cleanupBusy
                text: qsTr("Cancel")
                variant: "destructive"
                glyph: Theme.icons.x
                onClicked: EditorState.ai.cancelCleanup()
            }

            // --- what was found -----------------------------------------------------------
            Rectangle {
                width: controls.width
                height: Theme.c.subtitlesTab.borderWidth
                visible: EditorState.ai.cleanupFound
                color: Theme.c.subtitlesTab.border
            }

            Note {
                visible: EditorState.ai.cleanupFound
                text: EditorState.ai.cleanupHits.length === 0
                      ? qsTr("Nothing found with these settings.")
                      : qsTr("%1 found · %2 ticked · saves %3").arg(EditorState.ai.cleanupHits.length)
                            .arg(root.tickedCount).arg(root.seconds(root.savedSeconds))
                          + "\n" + qsTr("Click a row to hear it. Untick what should stay.")
            }

            Row {
                width: controls.width
                spacing: Theme.spacingSm
                visible: EditorState.ai.cleanupFound && EditorState.ai.cleanupHits.length > 0

                ThemedButton {
                    text: qsTr("Remove")
                    variant: "destructive"
                    glyph: Theme.icons.scissors
                    enabled: root.tickedCount > 0 && !EditorState.ai.cleanupBusy
                    tooltip: qsTr("Cut the ticked parts from the clip and its linked clips, closing the gaps (one undo step)")
                    onClicked: EditorState.ai.removeCleanupHits(root.tickedRows())
                }
                ThemedButton {
                    text: qsTr("All")
                    onClicked: root.tickAll(true)
                }
                ThemedButton {
                    text: qsTr("None")
                    onClicked: root.tickAll(false)
                }
            }
        }

        delegate: Rectangle {
            id: hit

            required property var modelData
            required property int index
            readonly property bool ticked: !!root.ticks[hit.index]

            width: hitList.width - hitScroll.width
            height: Theme.controlHeight
            radius: Theme.c.scenesShotRow.radius
            color: hitHover.hovered ? Theme.c.scenesShotRow.backgroundHover : "transparent"
            Accessible.role: Accessible.ListItem
            Accessible.name: hit.modelData.time + " " + hit.modelData.text

            ThemedCheckBox {
                id: tick
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                checked: hit.ticked
                tooltip: qsTr("Remove this one")
                onToggled: root.setTick(hit.index, checked)
            }

            Text {
                id: hitTime
                anchors.left: tick.right
                anchors.leftMargin: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                width: timeMetrics.advanceWidth + Theme.spacingSm
                text: hit.modelData.time
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            TextMetrics {
                id: timeMetrics
                font: hitTime.font
                text: "0:00:00"
            }

            Text {
                anchors.left: hitTime.right
                anchors.right: hitLength.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                elide: Text.ElideRight
                text: hit.modelData.kind === "silence" ? qsTr("Pause") : "“" + hit.modelData.text + "”"
                color: hit.modelData.kind === "silence" ? Theme.c.subtitlesTab.foregroundMuted
                                                        : Theme.c.subtitlesTab.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.italic: hit.modelData.kind === "silence"
            }

            Text {
                id: hitLength
                anchors.right: playIcon.left
                anchors.rightMargin: Theme.spacingXs
                anchors.verticalCenter: parent.verticalCenter
                text: root.seconds(hit.modelData.seconds)
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            IconGlyph {
                id: playIcon
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                glyph: Theme.icons.play
                iconSize: Theme.iconSizeSm
                iconColor: hitHover.hovered ? Theme.c.subtitlesTab.accent : Theme.c.subtitlesTab.foregroundMuted
            }

            HoverHandler {
                id: hitHover
                cursorShape: Qt.PointingHandCursor
            }

            TapHandler {
                // The checkbox takes its own clicks.
                enabled: !tick.hovered
                onTapped: EditorState.ai.playCleanupHit(hit.index)
            }
        }
    }
}
