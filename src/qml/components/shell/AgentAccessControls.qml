import QtQuick
import QtQuick.Controls.Basic
import Drift

// Agent access, written for someone who just wants it working: one button per assistant, the
// state, Stop, and one switch. Everything technical (what is opened, the helper, the secret
// address, manual setup, launch commands) is under "Details & security" for anyone who wants
// to check what Dafin is doing.
//
// One surface, two hosts: AgentAccessDialog (the header button) and the Agent access section
// of SettingsPane (where the phone reaches it), so the two never say different things.
Column {
    id: root

    property bool detailsOpen: false
    property bool manualOpen: false
    property string chatGptAddress: ""
    // The dialog leads with its own explanation; the settings section has a heading above it.
    property bool showIntro: true

    readonly property var chat: EditorState.mcp.chatGpt
    readonly property string chatState: chat ? chat.state : "idle"

    // The Claude / Cursor / Codex copies need access on; pressing one turns it on.
    function ensureOn() {
        if (!EditorState.mcp.running)
            EditorState.mcp.enabled = true
        return EditorState.mcp.running
    }

    spacing: Theme.spacingLg

    ThemedLabel {
        width: parent.width
        size: "sm"
        wrapMode: Text.WordWrap
        visible: root.showIntro
        text: qsTr("Let an AI assistant edit this project for you. Press the one you use.")
    }

    // ---- ChatGPT ----

    ThemedButton {
        width: parent.width
        variant: "primary"
        glyph: Theme.icons.linkTwo
        visible: root.chatState === "idle"
        text: qsTr("Connect ChatGPT")
        tooltip: qsTr("Dafin sets everything up, then gives you a prompt to paste into ChatGPT")
        onClicked: root.chat.start()
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.chatState !== "idle"

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: root.chatState !== "connected"
            tone: "default"
            size: "sm"
            color: root.chatState === "failed" ? Theme.c.agentAccessControls.foregroundError
                                               : Theme.c.agentAccessControls.foreground
            text: root.chatState === "downloading" && root.chat.progress > 0
                  ? qsTr("%1 %2%").arg(root.chat.message).arg(Math.round(root.chat.progress * 100))
                  : root.chat ? root.chat.message : ""
        }

        // Asked once: the helper is downloaded only after this OK.
        Row {
            spacing: Theme.spacingMd
            visible: root.chatState === "needsDownload"

            ThemedButton {
                variant: "primary"
                text: qsTr("Download and connect")
                onClicked: root.chat.confirmDownload()
            }
            ThemedButton {
                variant: "ghost"
                text: qsTr("Cancel")
                onClicked: root.chat.stop()
            }
        }

        Row {
            spacing: Theme.spacingMd
            visible: root.chatState === "failed"

            ThemedButton {
                variant: "primary"
                glyph: Theme.icons.refresh
                text: qsTr("Try again")
                onClicked: { root.chat.stop(); root.chat.start() }
            }
            ThemedButton {
                variant: "ghost"
                text: qsTr("Close")
                onClicked: root.chat.stop()
            }
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.chatState === "connected"

            Row {
                spacing: Theme.spacingMd

                IconGlyph {
                    glyph: Theme.icons.success
                    iconSize: Theme.iconSizeMd
                    iconColor: Theme.c.agentAccessControls.icon
                    anchors.verticalCenter: parent.verticalCenter
                }
                Text {
                    text: qsTr("Ready: ChatGPT can reach this project")
                    color: Theme.c.agentAccessControls.foreground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeSm
                    font.weight: Font.Medium
                    anchors.verticalCenter: parent.verticalCenter
                }
            }

            CopyButton {
                width: parent.width
                variant: "primary"
                label: qsTr("Copy prompt for ChatGPT")
                copiedText: qsTr("✓ Copied, now paste it into ChatGPT")
                copy: function () { return root.chat.copyPrompt() }
            }

            ThemedLabel {
                width: parent.width
                size: "sm"
                wrapMode: Text.WordWrap
                text: qsTr("Paste it into a new ChatGPT chat. ChatGPT tells you where to click, one step at a time.")
            }

            Flow {
                width: parent.width
                spacing: Theme.spacingMd

                ThemedButton {
                    variant: "secondary"
                    text: qsTr("Open ChatGPT")
                    onClicked: Qt.openUrlExternally("https://chatgpt.com/")
                }
                ThemedButton {
                    variant: "secondary"
                    text: root.chat.testState === "testing" ? qsTr("Testing…") : qsTr("Test connection")
                    enabled: root.chat.testState !== "testing"
                    tooltip: qsTr("Checks from the internet side that ChatGPT can reach Dafin")
                    onClicked: root.chat.test()
                }
                ThemedButton {
                    variant: "destructive"
                    text: qsTr("Stop")
                    tooltip: qsTr("Closes the connection. The copied address stops working.")
                    onClicked: root.chat.stop()
                }
            }

            ThemedLabel {
                width: parent.width
                size: "sm"
                wrapMode: Text.WordWrap
                visible: root.chat.testMessage.length > 0
                color: root.chat.testState === "failed" ? Theme.c.agentAccessControls.foregroundError
                                                        : Theme.c.agentAccessControls.foreground
                text: root.chat.testMessage
            }
        }
    }

    // ---- Assistants on this computer ----

    ThemedLabel {
        width: parent.width
        size: "sm"
        wrapMode: Text.WordWrap
        text: qsTr("Using an assistant on this computer? Copy its setup and paste it there:")
    }

    Flow {
        width: parent.width
        spacing: Theme.spacingMd

        CopyButton {
            variant: "secondary"
            label: qsTr("Claude")
            tooltip: qsTr("A command to paste into Claude Code")
            copy: function () { return root.ensureOn() && EditorState.mcp.copyClaudeCommand() }
        }
        CopyButton {
            variant: "secondary"
            label: qsTr("Cursor")
            tooltip: qsTr("A setup to paste into Cursor's MCP settings")
            copy: function () { return root.ensureOn() && EditorState.mcp.copyCursorSnippet() }
        }
        CopyButton {
            variant: "secondary"
            label: qsTr("Codex")
            tooltip: qsTr("A block to add to ~/.codex/config.toml")
            copy: function () { return root.ensureOn() && EditorState.mcp.copyCodexSnippet() }
        }
    }

    // ---- State and the one safety switch ----

    Row {
        spacing: Theme.spacingMd
        visible: EditorState.mcp.running

        Text {
            text: qsTr("● Agent access is on")
            color: Theme.c.agentAccessControls.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
            anchors.verticalCenter: parent.verticalCenter
        }
        ThemedButton {
            variant: "ghost"
            text: qsTr("Turn off")
            tooltip: qsTr("No assistant can edit this project until you turn it on again")
            onClicked: EditorState.mcp.enabled = false
        }
    }

    ThemedSwitch {
        checked: EditorState.mcp.confirmIrreversible
        text: qsTr("Ask me before anything that can't be undone")
        tooltip: qsTr("Before an agent overwrites a file, replaces the project from a snapshot, changes your snapping or shortcuts, or removes protected footage, Dafin asks you first.")
        onToggled: EditorState.mcp.confirmIrreversible = checked
    }

    ThemedLabel {
        width: parent.width
        wrapMode: Text.WordWrap
        visible: EditorState.mcp.error.length > 0
        text: EditorState.mcp.error
        color: Theme.c.agentAccessControls.foregroundError
    }

    // ---- Details & security ----

    ThemedButton {
        variant: "ghost"
        glyph: root.detailsOpen ? Theme.icons.chevronDown : Theme.icons.chevronRight
        text: qsTr("Details & security")
        onClicked: root.detailsOpen = !root.detailsOpen
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd
        visible: root.detailsOpen

        ThemedLabel {
            width: parent.width
            text: qsTr("What is opened on this computer")
            tone: "default"
            size: "sm"
            font.weight: Font.Medium
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Agent access opens a small server that only programs on this computer can reach (127.0.0.1). Every request needs a secret key, which is inside the setups you copy. Access is off each time Dafin starts, unless you choose otherwise below, and an agent can never confirm the “can't be undone” questions for you: only your click can.")
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            visible: EditorState.mcp.url.length > 0
            text: qsTr("Listening on %1").arg(EditorState.mcp.url)
        }
        ThemedSwitch {
            checked: EditorState.mcp.enabled
            text: qsTr("Allow for this session")
            tooltip: qsTr("Let an assistant on this device edit this project until you turn it off or quit.")
            onToggled: EditorState.mcp.enabled = checked
        }
        ThemedSwitch {
            visible: EditorState.mcp.running || EditorState.mcp.startOnLaunch
            checked: EditorState.mcp.startOnLaunch
            text: qsTr("Start agent access when Dafin opens")
            tooltip: qsTr("Turning access off resets this.")
            onToggled: EditorState.mcp.startOnLaunch = checked
        }
        ThemedButton {
            variant: "ghost"
            glyph: Theme.icons.refresh
            visible: EditorState.mcp.running
            text: qsTr("New key")
            tooltip: qsTr("Replace the key. Every assistant set up with the old one, ChatGPT's address included, stops working until you copy the setup again.")
            onClicked: {
                EditorState.mcp.rotateToken()
                Toasts.success(qsTr("New key made, copy the setup again"))
            }
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("How ChatGPT reaches Dafin")
            tone: "default"
            size: "sm"
            font.weight: Font.Medium
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("ChatGPT runs on OpenAI's servers, so it cannot reach this computer by itself. Dafin starts Cloudflare's free tunnel program (cloudflared) in the background, which gives a temporary public https address that forwards to Dafin. Only one secret path on it works, and only while the ChatGPT connection is on; anyone who has the full address can edit this project, so keep the prompt private and press Stop when you finish. Stop, New key, turning access off or quitting all close it. If Dafin downloads cloudflared, it comes from Cloudflare's official GitHub releases and runs only if its SHA-256 matches the checksum published there. Which ChatGPT plans can add a custom MCP server, and whether it may change things or only read, is decided by OpenAI.")
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WrapAnywhere
            visible: root.chat && root.chat.helperPath.length > 0
            text: root.chat ? qsTr("Helper: %1\n%2\nSHA-256: %3")
                              .arg(root.chat.helperPath).arg(root.chat.helperOrigin)
                              .arg(root.chat.helperChecksum) : ""
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WrapAnywhere
            visible: root.chat && root.chat.chatGptAddress.length > 0
            text: root.chat ? qsTr("ChatGPT address: %1").arg(root.chat.chatGptAddress) : ""
        }
        CopyButton {
            variant: "ghost"
            visible: root.chat && root.chat.chatGptAddress.length > 0
            label: qsTr("Copy only the address")
            copy: function () { return root.chat.copyAddress() }
        }
        TextArea {
            width: parent.width
            visible: root.chat && root.chat.helperLog.length > 0
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WrapAnywhere
            font.family: "monospace"
            font.pixelSize: Theme.fontSizeSm
            color: Theme.c.agentAccessControls.foreground
            text: root.chat ? root.chat.helperLog.split("\n").slice(-12).join("\n") : ""
        }

        ThemedButton {
            variant: "ghost"
            glyph: root.manualOpen ? Theme.icons.chevronDown : Theme.icons.chevronRight
            text: qsTr("Manual ChatGPT setup (run the tunnel yourself)")
            onClicked: root.manualOpen = !root.manualOpen
        }
        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.manualOpen

            ThemedSwitch {
                checked: EditorState.mcp.webAccess
                enabled: EditorState.mcp.running
                text: qsTr("1. Allow ChatGPT (web access)")
                tooltip: qsTr("Opens the secret address. It closes when you turn it off, turn access off or quit.")
                onToggled: EditorState.mcp.webAccess = checked
            }
            CopyButton {
                width: parent.width
                variant: "ghost"
                enabled: EditorState.mcp.webAccess
                label: qsTr("2. Copy the tunnel command, run it in a terminal")
                copy: function () { return EditorState.mcp.copyTunnelCommand() }
            }
            ThemedTextField {
                width: parent.width
                enabled: EditorState.mcp.webAccess
                placeholderText: qsTr("3. Paste the https:// address it prints")
                text: root.chatGptAddress
                onTextEdited: root.chatGptAddress = text
            }
            CopyButton {
                width: parent.width
                variant: "secondary"
                enabled: EditorState.mcp.chatGptUrl(root.chatGptAddress).length > 0
                label: qsTr("4. Copy the ChatGPT address and add it in ChatGPT")
                copy: function () { return EditorState.mcp.copyChatGptUrl(root.chatGptAddress) }
            }
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Help for the agent")
            tone: "default"
            size: "sm"
            font.weight: Font.Medium
        }
        CopyButton {
            width: parent.width
            variant: "ghost"
            label: qsTr("Copy a how-to for the agent")
            tooltip: qsTr("A short list of what the agent can do here; paste it into the chat")
            copy: function () { return EditorState.mcp.copyAgentGuide() }
        }
        CopyButton {
            width: parent.width
            variant: "ghost"
            label: qsTr("Copy one-time setup (other assistants)")
            tooltip: qsTr("Add this once to the assistant's config. Access still has to be turned on here.")
            copy: function () { return root.ensureOn() && EditorState.mcp.copyStdioSnippet() }
        }

        ThemedLabel {
            width: parent.width
            text: qsTr("Start Claude Code or Codex on a chosen model")
            tone: "default"
            size: "sm"
            font.weight: Font.Medium
        }
        ThemedLabel {
            width: parent.width
            size: "sm"
            wrapMode: Text.WordWrap
            text: qsTr("Most edits here are small, so a mid-size model is enough and a top model only uses up your limit faster. Dafin cannot change the model of an assistant that is already running; start it with the copied command. ChatGPT's model is chosen inside ChatGPT.")
        }
        ThemedComboBox {
            id: modelBox
            width: parent.width
            textRole: "label"
            valueRole: "value"
            model: [
                { label: qsTr("Claude: the app's own default"), value: "" },
                { label: qsTr("Claude Sonnet · recommended for most edits"), value: "sonnet" },
                { label: qsTr("Claude Opus · harder jobs, uses more of your limit"), value: "opus" },
                { label: qsTr("Claude Haiku · fastest and cheapest"), value: "haiku" }
            ]
            currentIndex: {
                var current = EditorState.mcp.assistantModel
                for (var i = 0; i < modelBox.model.length; ++i) {
                    if (modelBox.model[i].value === current)
                        return i
                }
                return 0
            }
            onActivated: index => EditorState.mcp.assistantModel = modelBox.model[index].value
        }
        CopyButton {
            width: parent.width
            variant: "ghost"
            label: qsTr("Copy Claude launch command")
            copy: function () { return EditorState.mcp.copyClaudeLaunchCommand() }
        }
        ModelPicker {
            width: parent.width
            options: EditorState.mcp.codexModelOptions
            current: EditorState.mcp.codexModel
            placeholder: qsTr("Codex model name")
            onPicked: value => EditorState.mcp.codexModel = value
        }
        CopyButton {
            width: parent.width
            variant: "ghost"
            label: qsTr("Copy Codex launch command")
            copy: function () { return EditorState.mcp.copyCodexLaunchCommand() }
        }
    }
}
