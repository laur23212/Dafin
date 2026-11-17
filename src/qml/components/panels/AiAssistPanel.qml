pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Drift

// Dafin AI Assist: type a request, and a model edits the open project through Dafin's own tools
// (AssistantController). Opened by the Agent button in the header. Until a provider is set up
// and the user has agreed to what is sent, it shows the setup view and makes no network call.
Popup {
    id: root

    // Height of the header above it, so the panel starts below it.
    property real topInset: 0
    property bool showSettings: false
    signal openAgentAccess()

    parent: Overlay.overlay
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: Math.min(440, parent ? parent.width - Theme.spacingLg * 2 : 440)
    height: parent ? parent.height - topInset - Theme.spacingLg : 600
    x: parent ? parent.width - width - Theme.spacingSm : 0
    y: topInset

    readonly property bool setupNeeded: !Assistant.ready

    function toggle() {
        if (opened)
            close()
        else
            open()
    }

    function sendPrompt() {
        const text = promptBox.text
        if (text.trim().length === 0 || Assistant.busy)
            return
        Assistant.send(text)
        promptBox.text = ""
    }

    onOpened: {
        if (!setupNeeded && !showSettings)
            promptBox.forceActiveFocus()
    }

    background: Rectangle {
        color: Theme.panelBackground
        border.color: Theme.panelBorder
        radius: Theme.radiusMd
    }

    contentItem: ColumnLayout {
        spacing: 0

        // ---- Header ----
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            ThemedLabel {
                text: qsTr("AI Assist")
                tone: "default"
                size: "base"
            }
            ThemedLabel {
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: Assistant.model
            }
            IconButton {
                glyph: Theme.icons.plus
                tooltip: qsTr("New chat")
                Accessible.name: qsTr("New chat")
                enabled: Assistant.messages.length > 0
                onClicked: Assistant.newChat()
            }
            IconButton {
                glyph: Theme.icons.settings
                tooltip: qsTr("AI Assist settings")
                Accessible.name: qsTr("AI Assist settings")
                active: root.showSettings
                onClicked: root.showSettings = !root.showSettings
            }
            IconButton {
                glyph: Theme.icons.x
                tooltip: qsTr("Close")
                Accessible.name: qsTr("Close AI Assist")
                onClicked: root.close()
            }
        }

        Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.panelBorder }

        // ---- Setup / settings ----
        Flickable {
            id: settingsView
            visible: root.showSettings || root.setupNeeded
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: settingsColumn.implicitHeight + Theme.spacingLg * 2
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            ColumnLayout {
                id: settingsColumn
                x: Theme.spacingLg
                y: Theme.spacingLg
                width: settingsView.width - Theme.spacingLg * 2
                spacing: Theme.spacingMd

                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: root.setupNeeded
                    tone: "default"
                    size: "sm"
                    text: qsTr("Set up AI Assist to edit this project by typing what you want. You need your own API key, or a local AI server.")
                }

                ThemedLabel { text: qsTr("AI service") }
                ThemedComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("Anthropic (Claude)"), qsTr("OpenAI-compatible (OpenAI, Ollama, LM Studio…)")]
                    currentIndex: Assistant.provider === "openai" ? 1 : 0
                    Accessible.name: qsTr("AI service")
                    onActivated: index => Assistant.provider = index === 1 ? "openai" : "anthropic"
                }

                ThemedLabel { text: qsTr("Model") }
                ThemedTextField {
                    Layout.fillWidth: true
                    text: Assistant.model
                    placeholderText: Assistant.provider === "openai" ? qsTr("e.g. the model name your server lists")
                                                                     : qsTr("e.g. claude-sonnet-5-5")
                    Accessible.name: qsTr("Model name")
                    onEditingFinished: Assistant.model = text
                }

                ThemedLabel { visible: Assistant.provider === "openai"; text: qsTr("Server address (leave empty for OpenAI)") }
                ThemedTextField {
                    visible: Assistant.provider === "openai"
                    Layout.fillWidth: true
                    text: Assistant.baseUrl
                    placeholderText: "http://localhost:11434/v1"
                    Accessible.name: qsTr("Server address")
                    onEditingFinished: Assistant.baseUrl = text
                }

                ThemedLabel { text: qsTr("API key") }
                ThemedLabel {
                    visible: Assistant.keyFromEnvironment
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Using the key from the %1 environment variable.")
                          .arg(Assistant.provider === "openai" ? "OPENAI_API_KEY" : "ANTHROPIC_API_KEY")
                }
                RowLayout {
                    visible: !Assistant.keyFromEnvironment && Assistant.hasKey
                    Layout.fillWidth: true
                    ThemedLabel { Layout.fillWidth: true; text: qsTr("Key saved on this computer.") }
                    ThemedButton {
                        text: qsTr("Remove key")
                        onClicked: Assistant.clearApiKey()
                    }
                }
                RowLayout {
                    visible: !Assistant.keyFromEnvironment && !Assistant.hasKey
                    Layout.fillWidth: true
                    ThemedTextField {
                        id: keyField
                        Layout.fillWidth: true
                        echoMode: TextInput.Password
                        placeholderText: Assistant.provider === "openai" ? qsTr("Optional for a local server") : "sk-ant-…"
                        Accessible.name: qsTr("API key")
                        onAccepted: saveKey.clicked()
                    }
                    ThemedButton {
                        id: saveKey
                        text: qsTr("Save key")
                        enabled: keyField.text.trim().length > 0
                        onClicked: {
                            keySaveError.visible = !Assistant.setApiKey(keyField.text)
                            keyField.text = ""
                        }
                    }
                }
                ThemedLabel {
                    id: keySaveError
                    visible: false
                    color: Theme.destructive
                    text: qsTr("Couldn't save the key.")
                }
                ThemedLabel {
                    visible: !Assistant.keyFromEnvironment && !Assistant.keyStorageEncrypted
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("On this system the key is kept in a file only your account can read; it is not encrypted.")
                }

                ThemedSwitch {
                    checked: Assistant.allowFrames
                    text: qsTr("Allow the assistant to see frames")
                    tooltip: qsTr("Lets the model look at pictures from your footage to check its edits. Off by default; the pictures are sent to the AI service.")
                    onToggled: Assistant.allowFrames = checked
                }

                ThemedLabel { text: qsTr("Editing knowledge folder (optional)") }
                ThemedTextField {
                    Layout.fillWidth: true
                    text: Assistant.knowledgeFolder
                    placeholderText: qsTr("Folder with a SKILL.md, e.g. your Taste Engine")
                    Accessible.name: qsTr("Editing knowledge folder")
                    onEditingFinished: Assistant.knowledgeFolder = text
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.panelBorder }

                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    size: "sm"
                    tone: "default"
                    text: Assistant.consentText
                }
                ThemedCheckBox {
                    text: qsTr("I understand and agree")
                    checked: Assistant.consent
                    onToggled: Assistant.consent = checked
                }

                ThemedLabel {
                    visible: root.setupNeeded
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.destructive
                    text: Assistant.notReadyReason
                }

                RowLayout {
                    Layout.fillWidth: true
                    ThemedButton {
                        text: qsTr("Connect Claude or Cursor instead…")
                        onClicked: root.openAgentAccess()
                    }
                    Item { Layout.fillWidth: true }
                    ThemedButton {
                        visible: !root.setupNeeded
                        text: qsTr("Done")
                        onClicked: {
                            root.showSettings = false
                            promptBox.forceActiveFocus()
                        }
                    }
                }

                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    visible: Assistant.usageLogPath.length > 0
                    text: qsTr("Token use per request is saved to %1").arg(Assistant.usageLogPath)
                }
            }
        }

        // ---- Chat ----
        ListView {
            id: chat
            visible: !settingsView.visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.spacingSm
            topMargin: Theme.spacingMd
            bottomMargin: Theme.spacingMd
            model: Assistant.messages
            onCountChanged: Qt.callLater(chat.positionViewAtEnd)

            ThemedLabel {
                anchors.centerIn: parent
                width: parent.width - Theme.spacingLg * 4
                visible: chat.count === 0
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                size: "sm"
                text: qsTr("Ask for an edit, for example: “Cut the silences longer than a second” or “Add captions in a bold style”. Every change can be undone.")
            }

            delegate: Item {
                id: row
                required property var modelData
                readonly property string kind: modelData.kind
                width: chat.width
                implicitHeight: body.implicitHeight + (kind === "user" ? Theme.spacingMd * 2 : 0)

                Rectangle {
                    visible: row.kind === "user"
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    width: Math.min(body.implicitWidth + Theme.spacingMd * 2, row.width * 0.85)
                    height: parent.height
                    radius: Theme.radiusMd
                    color: Theme.panelSecondaryBg
                }

                TextEdit {
                    id: body
                    readonly property real maxWidth: row.kind === "user" ? row.width * 0.85 - Theme.spacingMd * 3
                                                                        : row.width - Theme.spacingMd * 2
                    x: row.kind === "user" ? row.width - Math.min(implicitWidth, maxWidth) - Theme.spacingMd * 2
                                           : Theme.spacingMd
                    y: row.kind === "user" ? Theme.spacingMd : 0
                    width: Math.min(implicitWidth, maxWidth)
                    readOnly: true
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    textFormat: row.kind === "assistant" ? TextEdit.MarkdownText : TextEdit.PlainText
                    font.family: Theme.fontFamily
                    font.pixelSize: row.kind === "step" || row.kind === "answer" ? Theme.fontSizeXs : Theme.fontSizeSm
                    color: row.kind === "error" ? Theme.destructive
                         : (row.kind === "step" || row.kind === "info" || row.kind === "answer")
                           ? Theme.mutedForeground : Theme.panelForeground
                    text: row.kind === "step"
                          ? (row.modelData.ok ? "✓ " : "✗ ") + row.modelData.text + "  ·  " + (row.modelData.detail || "")
                          : row.kind === "answer" ? qsTr("You: %1").arg(row.modelData.text)
                          : row.modelData.text
                    Accessible.name: text
                }
            }
        }

        // ---- Open question: only these buttons can answer it ----
        Rectangle {
            visible: !settingsView.visible && (Assistant.question.id || "").length > 0
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            implicitHeight: questionColumn.implicitHeight + Theme.spacingMd * 2
            radius: Theme.radiusMd
            color: Theme.panelSecondaryBg

            ColumnLayout {
                id: questionColumn
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingSm

                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    tone: "default"
                    size: "sm"
                    text: Assistant.question.text || ""
                }
                ThemedLabel {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    visible: (Assistant.question.why || "").length > 0
                    text: Assistant.question.why || ""
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Theme.spacingSm
                    Repeater {
                        model: Assistant.question.options || []
                        ThemedButton {
                            required property string modelData
                            required property int index
                            text: modelData
                            onClicked: Assistant.answerQuestion(Assistant.question.id, index)
                        }
                    }
                    ThemedButton {
                        text: qsTr("Skip")
                        variant: "secondary"
                        onClicked: Assistant.answerQuestion(Assistant.question.id, -1)
                    }
                    ThemedButton {
                        text: qsTr("Not sure")
                        variant: "secondary"
                        onClicked: Assistant.answerQuestion(Assistant.question.id, -2)
                    }
                }
            }
        }

        // ---- Prompt ----
        Rectangle { visible: !settingsView.visible; Layout.fillWidth: true; implicitHeight: 1; color: Theme.panelBorder }
        RowLayout {
            visible: !settingsView.visible
            Layout.fillWidth: true
            Layout.margins: Theme.spacingMd
            spacing: Theme.spacingSm

            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(Math.max(promptBox.implicitHeight, 40), 160)
                ThemedTextArea {
                    id: promptBox
                    placeholderText: qsTr("Ask AI Assist to edit this project…  (Enter sends, Shift+Enter new line)")
                    Accessible.name: qsTr("Message to AI Assist")
                    Keys.onReturnPressed: event => {
                        if (event.modifiers & Qt.ShiftModifier) {
                            event.accepted = false
                            return
                        }
                        event.accepted = true
                        root.sendPrompt()
                    }
                    Keys.onEnterPressed: event => {
                        event.accepted = true
                        root.sendPrompt()
                    }
                }
            }
            ThemedButton {
                visible: !Assistant.busy
                variant: "primary"
                text: qsTr("Send")
                enabled: promptBox.text.trim().length > 0 && (Assistant.question.id || "").length === 0
                onClicked: root.sendPrompt()
            }
            ThemedButton {
                visible: Assistant.busy
                text: qsTr("Stop")
                variant: "destructive"
                onClicked: Assistant.stop()
            }
        }
        ThemedLabel {
            visible: !settingsView.visible && Assistant.lastUsage.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: Theme.spacingMd
            Layout.rightMargin: Theme.spacingMd
            Layout.bottomMargin: Theme.spacingSm
            elide: Text.ElideRight
            text: Assistant.lastUsage
        }
    }
}
