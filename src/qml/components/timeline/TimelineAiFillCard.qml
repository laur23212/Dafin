pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dafin AI Fill: the card that floats next to the marked space (TimelineAiFillOverlay) — where
// the user describes the animation, watches it being made, and moves between its versions.
// A non-modal popup so it can sit over the preview above a short timeline without being clipped.
Popup {
    id: card

    property var host           // TimelineAiFillOverlay
    property Item anchorItem    // the marked space

    readonly property string phase: host ? host.phase : ""
    readonly property bool changing: host && host.mode === "change"
    readonly property var fillState: Assistant.fill

    parent: Overlay.overlay
    visible: !!host && host.regionShown && !host.drawing && phase !== ""
    modal: false
    focus: true
    closePolicy: Popup.NoAutoClose
    padding: Theme.spacingMd
    width: Math.min(400, parent ? parent.width - Theme.spacingLg * 2 : 400)

    // Above the space when there is room (over the preview, like a contextual task bar), else
    // below it; always inside the window and pointing at the visible part of the space.
    x: {
        if (!anchorItem || !parent || !host)
            return 0
        host.view.contentX; host.regionStart; host.regionEnd; host.pxPerSecond; parent.width
        const viewLeft = host.view.contentX
        const viewRight = viewLeft + host.view.width
        const left = Math.max(host.regionX, viewLeft)
        const right = Math.min(host.regionX + host.regionW, viewRight)
        const centre = right > left ? (left + right) / 2 : Math.min(Math.max(host.regionX, viewLeft), viewRight)
        const p = host.mapToItem(parent, centre, 0)
        return Math.max(Theme.spacingSm, Math.min(p.x - width / 2, parent.width - width - Theme.spacingSm))
    }
    y: {
        if (!anchorItem || !parent || !host)
            return 0
        host.view.contentY; host.regionY; host.regionH; height; parent.height
        const top = host.mapToItem(parent, 0, host.regionY).y
        const bottom = host.mapToItem(parent, 0, host.regionY + host.regionH).y
        const above = top - height - Theme.spacingSm
        if (above >= Theme.spacingSm)
            return above
        return Math.min(bottom + Theme.spacingSm, parent.height - height - Theme.spacingSm)
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: Theme.durationFast; easing.type: Theme.easing }
    }

    onPhaseChanged: {
        if (phase === "prompt")
            Qt.callLater(() => {
                promptBox.text = card.changing ? card.host.changeText : card.host.promptText
                promptBox.forceActiveFocus()
                promptBox.cursorPosition = promptBox.text.length
            })
    }

    function submit() {
        if (!Assistant.ready) {
            ShellBridge.aiAssistSetupRequested()
            return
        }
        host.generate(promptBox.text, false)
    }
    function useExample(text) {
        promptBox.text = promptBox.text.trim().length === 0 ? text : promptBox.text.trim() + ". " + text
        promptBox.forceActiveFocus()
        promptBox.cursorPosition = promptBox.text.length
    }

    readonly property var newExamples: [
        { label: qsTr("Lower third"), text: qsTr("A lower third sliding in from the left with a name and a role: \"Name — Role\"") },
        { label: qsTr("Title"), text: qsTr("A bold title that pops in word by word: \"Your title here\"") },
        { label: qsTr("Arrow"), text: qsTr("An arrow that draws itself in and points at the middle of the screen") },
        { label: qsTr("Counter"), text: qsTr("A number counting up from 0 to 100, with a short label under it") },
        { label: qsTr("Subscribe"), text: qsTr("A subscribe button that pops in, gets clicked by a cursor, then turns grey") },
        { label: qsTr("Progress bar"), text: qsTr("A thin progress bar along the bottom edge that fills across the whole space") }
    ]
    readonly property var changeExamples: [
        { label: qsTr("Slower"), text: qsTr("Make it slower and calmer") },
        { label: qsTr("Faster"), text: qsTr("Make it faster and snappier") },
        { label: qsTr("Bigger text"), text: qsTr("Make the text bigger") },
        { label: qsTr("Other colours"), text: qsTr("Use different colours") },
        { label: qsTr("Simpler"), text: qsTr("Make it simpler, with less going on") },
        { label: qsTr("More energy"), text: qsTr("Give it more energy and bounce") }
    ]

    background: Rectangle {
        color: Theme.panelBackground
        border.color: card.phase === "error" ? Theme.destructive : Theme.panelBorder
        radius: Theme.radiusLg
        // A thin accent line on top ties the card to the dashed space below it.
        Rectangle {
            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - parent.radius * 2
            height: 2
            radius: 1
            color: card.phase === "error" ? Theme.destructive : Theme.accent
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingSm
        Keys.onEscapePressed: (event) => {
            event.accepted = true
            if (card.phase === "working")
                return
            if (card.phase === "prompt" && card.changing) {
                card.host.mode = "new"
                card.host.phase = "done"
                return
            }
            card.host.close()
        }

        // ---- Header: what this is, how long the space is, Redraw, Close ----
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            IconGlyph {
                glyph: Theme.icons.sparkles
                iconSize: 16
                iconColor: Theme.accent
                spinning: card.phase === "working"
            }
            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                size: "sm"
                font.weight: Font.Medium
                elide: Text.ElideRight
                text: {
                    switch (card.phase) {
                    case "working": return qsTr("Making it…")
                    case "done": return qsTr("AI Fill")
                    case "error": return qsTr("That didn't work")
                    }
                    return card.changing ? qsTr("Change version %1").arg(card.host.versionsInfo.current + 1)
                                         : qsTr("Fill %1 with…").arg(card.host.lengthText())
                }
            }
            IconButton {
                visible: card.phase === "prompt" || card.phase === "error"
                glyph: Theme.icons.refresh
                text: qsTr("Redraw")
                tooltip: qsTr("Draw the space again. Your description is kept")
                Accessible.name: qsTr("Redraw the space")
                onClicked: card.host.redraw()
            }
            IconButton {
                visible: card.phase !== "working"
                glyph: Theme.icons.x
                tooltip: card.phase === "done" ? qsTr("Close. The animation stays on the timeline")
                                               : qsTr("Cancel (Esc)")
                Accessible.name: qsTr("Close AI Fill")
                onClicked: card.host.close()
            }
        }

        ThemedLabel {
            visible: card.host && card.host.note.length > 0
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: card.host ? card.host.note : ""
        }

        // ---- Describe it ----
        ColumnLayout {
            visible: card.phase === "prompt"
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(promptBox.implicitHeight, 56), 140)
                ThemedTextArea {
                    id: promptBox
                    wrapMode: TextEdit.Wrap
                    placeholderText: card.changing
                        ? qsTr("What should change? e.g. slower, bigger text, blue instead of teal")
                        : qsTr("Describe what goes here, e.g. a lower third that says \"Ana Pop — Designer\". You can ask for more too, like arrows on the next clip")
                    Accessible.name: qsTr("Describe the animation")
                    Keys.onReturnPressed: (event) => {
                        if (event.modifiers & Qt.ShiftModifier) {
                            event.accepted = false
                            return
                        }
                        event.accepted = true
                        card.submit()
                    }
                    Keys.onEnterPressed: (event) => {
                        event.accepted = true
                        card.submit()
                    }
                }
            }

            Flow {
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                Repeater {
                    model: card.changing ? card.changeExamples : card.newExamples
                    delegate: ThemedButton {
                        required property var modelData
                        variant: "ghost"
                        text: modelData.label
                        onClicked: card.useExample(modelData.text)
                    }
                }
            }

            ThemedLabel {
                visible: !Assistant.ready
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("AI Fill uses AI Assist. Set up an AI service once, and it works from here.")
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                ThemedButton {
                    visible: card.changing
                    text: qsTr("Back")
                    onClicked: {
                        card.host.mode = "new"
                        card.host.phase = "done"
                    }
                }
                ThemedLabel {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    text: card.changing ? qsTr("The current version is kept") : qsTr("Enter to make it · Shift+Enter for a new line")
                }
                ThemedButton {
                    variant: "primary"
                    glyph: Theme.icons.sparkles
                    text: !Assistant.ready ? qsTr("Set up AI Assist…")
                          : card.changing ? qsTr("Make new version") : qsTr("Generate")
                    enabled: !Assistant.ready || (promptBox.text.trim().length > 0 && !Assistant.busy)
                    onClicked: card.submit()
                }
            }
        }

        // ---- Being made ----
        ColumnLayout {
            visible: card.phase === "working"
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                wrapMode: Text.WordWrap
                text: card.fillState.status || qsTr("Designing the animation…")
            }
            ThemedLabel {
                Layout.fillWidth: true
                maximumLineCount: 2
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                text: qsTr("“%1”").arg(card.fillState.prompt || "")
            }
            // The AI's one question, answered only by these buttons (as in the chat panel).
            ColumnLayout {
                readonly property var question: Assistant.question
                visible: (question.id || "").length > 0
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                ThemedLabel {
                    Layout.fillWidth: true
                    tone: "default"
                    size: "sm"
                    wrapMode: Text.WordWrap
                    text: parent.question.text || ""
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingXs
                    Repeater {
                        model: Assistant.question.options || []
                        delegate: ThemedButton {
                            required property string modelData
                            required property int index
                            variant: "primary"
                            text: modelData
                            onClicked: Assistant.answerQuestion(Assistant.question.id, index)
                        }
                    }
                    ThemedButton {
                        text: qsTr("Skip")
                        onClicked: Assistant.answerQuestion(Assistant.question.id, -1)
                    }
                    ThemedButton {
                        text: qsTr("Not sure")
                        onClicked: Assistant.answerQuestion(Assistant.question.id, -2)
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                ThemedLabel {
                    Layout.fillWidth: true
                    text: qsTr("Usually 20–60 seconds")
                }
                ThemedButton {
                    variant: "destructive"
                    glyph: Theme.icons.square
                    text: qsTr("Stop")
                    onClicked: Assistant.stop()
                }
            }
        }

        // ---- A result, and its versions ----
        ColumnLayout {
            visible: card.phase === "done"
            Layout.fillWidth: true
            spacing: Theme.spacingSm

            RowLayout {
                visible: info.ok
                Layout.fillWidth: true
                spacing: Theme.spacingXs
                readonly property var info: card.host ? card.host.versionsInfo : ({ ok: false })
                IconButton {
                    glyph: Theme.icons.chevronLeft
                    tooltip: qsTr("Previous version")
                    Accessible.name: qsTr("Previous version")
                    enabled: parent.info.ok && parent.info.current > 0
                    onClicked: card.host.switchVersion(-1)
                }
                ThemedLabel {
                    tone: "default"
                    size: "sm"
                    text: parent.info.ok ? qsTr("Version %1 of %2").arg(parent.info.current + 1).arg(parent.info.versions.length)
                                         : ""
                }
                IconButton {
                    glyph: Theme.icons.chevronRight
                    tooltip: qsTr("Next version")
                    Accessible.name: qsTr("Next version")
                    enabled: parent.info.ok && parent.info.current < parent.info.versions.length - 1
                    onClicked: card.host.switchVersion(1)
                }
                Item { Layout.fillWidth: true }
            }
            ThemedLabel {
                visible: card.host && card.host.versionsInfo.ok
                Layout.fillWidth: true
                tone: "default"
                maximumLineCount: 3
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                text: qsTr("“%1”").arg(card.host ? card.host.currentPrompt() : "")
            }
            // What the AI said it did (it may have worked beyond the space).
            ThemedLabel {
                visible: text.length > 0
                Layout.fillWidth: true
                tone: card.host && card.host.versionsInfo.ok ? "muted" : "default"
                maximumLineCount: 4
                elide: Text.ElideRight
                wrapMode: Text.WordWrap
                text: card.fillState.text || ""
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                ThemedButton {
                    visible: !card.host.versionsInfo.ok
                    glyph: Theme.icons.sparkles
                    text: qsTr("Ask for more…")
                    tooltip: qsTr("Another request for this space")
                    enabled: !Assistant.busy
                    onClicked: {
                        card.host.mode = "new"
                        card.host.promptText = ""
                        card.host.note = ""
                        card.host.phase = "prompt"
                    }
                }
                ThemedButton {
                    visible: card.host.versionsInfo.ok
                    glyph: Theme.icons.pencil
                    text: qsTr("Change it…")
                    enabled: !Assistant.busy
                    onClicked: {
                        card.host.mode = "change"
                        card.host.changeText = ""
                        card.host.note = ""
                        card.host.phase = "prompt"
                    }
                }
                ThemedButton {
                    visible: card.host.versionsInfo.ok
                    glyph: Theme.icons.refresh
                    text: qsTr("Try again")
                    tooltip: qsTr("A different take on the same request, as a new version")
                    enabled: !Assistant.busy && Assistant.ready
                    onClicked: card.host.generate(card.host.currentPrompt(), true)
                }
                Item { Layout.fillWidth: true }
                ThemedButton {
                    variant: "primary"
                    glyph: Theme.icons.check
                    text: qsTr("Done")
                    onClicked: card.host.close()
                }
            }
            ThemedLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: card.host && card.host.versionsInfo.ok
                      ? qsTr("Every version is kept with the project. Other changes undo with Ctrl+Z.")
                      : card.fillState.changed ? qsTr("The changes are on the timeline. Ctrl+Z undoes them step by step.")
                                               : qsTr("Nothing in the project changed.")
            }
        }

        // ---- It did not work ----
        ColumnLayout {
            visible: card.phase === "error"
            Layout.fillWidth: true
            spacing: Theme.spacingSm
            ThemedLabel {
                Layout.fillWidth: true
                tone: "default"
                wrapMode: Text.WordWrap
                text: card.fillState.error || qsTr("Something went wrong. Nothing on the timeline changed.")
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingSm
                ThemedButton {
                    visible: !!card.fillState.setup
                    variant: "primary"
                    text: qsTr("Set up AI Assist…")
                    onClicked: ShellBridge.aiAssistSetupRequested()
                }
                ThemedButton {
                    visible: !card.fillState.setup
                    glyph: Theme.icons.refresh
                    text: qsTr("Try again")
                    enabled: !Assistant.busy
                    onClicked: card.host.generate(card.fillState.prompt || card.host.promptText,
                                                  card.host.clipId !== "" && !card.changing)
                }
                ThemedButton {
                    glyph: Theme.icons.pencil
                    text: qsTr("Edit the request")
                    onClicked: card.host.phase = "prompt"
                }
                Item { Layout.fillWidth: true }
            }
        }
    }
}
