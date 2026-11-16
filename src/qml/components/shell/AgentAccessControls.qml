import QtQuick
import QtQuick.Controls.Basic
import Drift

// Localhost MCP, written for the person connecting an assistant rather than for someone
// reading a protocol spec. Access is per session; the key persists so a pasted setup keeps
// working across launches.
//
// One surface, two hosts: AgentAccessDialog (the desktop header button) and the Agent
// access section of SettingsPane, which is where the phone reaches it. Phase 0 added a
// cut-down copy of this to SettingsPane, and the two immediately said different things
// about the same switch — the mobile copy offered only the Claude command, so an "Allow
// for this session" toggled there could not be connected from Cursor at all.
Column {
    id: root

    property bool detailsOpen: false
    property bool chatGptOpen: false
    property string chatGptAddress: ""
    // The dialog leads with its own explanation; the settings section has a heading above
    // it and does not need a second one.
    property bool showIntro: true

    spacing: Theme.spacingXl

    ThemedLabel {
        width: parent.width
        size: "sm"
        wrapMode: Text.WordWrap
        visible: root.showIntro
        text: qsTr("Let Cursor or Claude edit this project for you — add clips, change the timeline, and check how it looks. Only programs on this device. Off by default each time you open Drift, unless you turn on “Start agent on startup” below; turn it off here when you finish. The key stays the same between sessions, so a setup you pasted once keeps working.")
    }

    ThemedSwitch {
        checked: EditorState.mcp.enabled
        text: qsTr("Allow for this session")
        tooltip: qsTr("Let an assistant on this device edit this project until you turn it off or quit.")
        onToggled: EditorState.mcp.enabled = checked
    }

    // Visible whenever there is something to act on: normally that means access is
    // currently on, but a start-on-launch attempt that failed leaves startOnLaunch
    // set and running false — the switch has to stay reachable then too, or turning
    // it back off (to stop the next launch from trying again) has nowhere to happen.
    ThemedSwitch {
        visible: EditorState.mcp.running || EditorState.mcp.startOnLaunch
        checked: EditorState.mcp.startOnLaunch
        text: qsTr("Start agent on startup")
        tooltip: qsTr("Skip the manual toggle next time you open Drift. Turning access off resets this.")
        onToggled: EditorState.mcp.startOnLaunch = checked
    }

    // Dafin: on by default. Only this switch changes it; no agent op can.
    ThemedSwitch {
        checked: EditorState.mcp.confirmIrreversible
        text: qsTr("Ask before irreversible agent actions")
        tooltip: qsTr("Before an agent overwrites a file, replaces the project from a snapshot, changes your snapping or shortcuts, or removes protected footage, Dafin asks you first.")
        onToggled: EditorState.mcp.confirmIrreversible = checked
    }

    ThemedLabel {
        width: parent.width
        visible: EditorState.mcp.error.length > 0
        text: EditorState.mcp.error
        color: Theme.c.agentAccessControls.foregroundError
    }

    ThemedLabel {
        width: parent.width
        visible: !EditorState.mcp.running
        wrapMode: Text.WordWrap
        text: qsTr("Turn this on, then copy the setup for Cursor or Claude and paste it into that app.")
    }

    Column {
        width: parent.width
        spacing: Theme.spacingLg
        visible: EditorState.mcp.running

        Row {
            spacing: Theme.spacingMd

            IconGlyph {
                glyph: Theme.icons.success
                iconSize: Theme.iconSizeMd
                iconColor: Theme.c.agentAccessControls.icon
                anchors.verticalCenter: parent.verticalCenter
            }

            Text {
                text: qsTr("Access is on")
                color: Theme.c.agentAccessControls.foreground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
                font.weight: Font.Medium
                anchors.verticalCenter: parent.verticalCenter
            }
        }

        // The address alone is not enough to connect — the server rejects any request
        // without the session token — so the copyable setup has to sit beside the switch.
        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            visible: EditorState.mcp.url.length > 0
            size: "sm"
            text: qsTr("Listening on %1").arg(EditorState.mcp.url)
        }

        ThemedButton {
            variant: "ghost"
            glyph: Theme.icons.refresh
            text: qsTr("New key")
            tooltip: qsTr("Replace the key. Every assistant set up with the old one stops working until you copy the setup again.")
            onClicked: {
                EditorState.mcp.rotateToken()
                Toasts.success(qsTr("New key made — copy the setup again"))
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Copy the setup for the assistant you use. You only need one.")
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.copy
            text: qsTr("Copy for Cursor")
            tooltip: qsTr("Copy a setup snippet to paste into Cursor")
            onClicked: {
                EditorState.mcp.copyCursorSnippet()
                Toasts.success(qsTr("Copied for Cursor"))
            }
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.copy
            text: qsTr("Copy for Claude")
            tooltip: qsTr("Copy a command to paste into Claude Code")
            onClicked: {
                EditorState.mcp.copyClaudeCommand()
                Toasts.success(qsTr("Copied for Claude"))
            }
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.copy
            text: qsTr("Copy for Codex")
            tooltip: qsTr("Copy a block to add to Codex's config.toml (the Codex app, command line and editor extension share it)")
            onClicked: {
                EditorState.mcp.copyCodexSnippet()
                Toasts.success(qsTr("Copied for Codex — add it to ~/.codex/config.toml"))
            }
        }

        ThemedButton {
            width: parent.width
            variant: "secondary"
            glyph: Theme.icons.linkTwo
            text: root.chatGptOpen ? qsTr("Connect to ChatGPT ▾") : qsTr("Connect to ChatGPT…")
            tooltip: qsTr("Let ChatGPT on the website edit this project, through a tunnel you start")
            onClicked: root.chatGptOpen = !root.chatGptOpen
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.chatGptOpen

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("ChatGPT runs on a website, so it cannot reach this computer by itself. Dafin opens a private address and a small free tunnel program (cloudflared) makes it reachable. Anyone who has the final address can edit this project, so turn this off when you finish. Which ChatGPT plans can add a custom connector, and whether it may change things or only read, is decided by OpenAI.")
            }

            ThemedSwitch {
                checked: EditorState.mcp.webAccess
                text: qsTr("1. Allow ChatGPT (web access)")
                tooltip: qsTr("Opens a secret address on this computer. It closes when you turn it off, turn access off or quit.")
                onToggled: EditorState.mcp.webAccess = checked
            }

            Column {
                width: parent.width
                spacing: Theme.spacingMd
                visible: EditorState.mcp.webAccess

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("2. Copy this command, paste it into a terminal and leave it running. It prints an address that starts with https://.")
                }

                ThemedButton {
                    width: parent.width
                    variant: "ghost"
                    glyph: Theme.icons.copy
                    text: qsTr("Copy tunnel command")
                    onClicked: {
                        EditorState.mcp.copyTunnelCommand()
                        Toasts.success(qsTr("Copied tunnel command"))
                    }
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("3. Paste the https:// address the tunnel printed:")
                }

                ThemedTextField {
                    width: parent.width
                    placeholderText: qsTr("https://example.trycloudflare.com")
                    text: root.chatGptAddress
                    onTextEdited: root.chatGptAddress = text
                }

                ThemedLabel {
                    width: parent.width
                    wrapMode: Text.WordWrap
                    text: qsTr("4. Copy the ChatGPT address, then in ChatGPT open Settings › Connectors › Advanced, turn on Developer mode, press Create, paste the address, choose “No authentication” and confirm you trust it.")
                }

                ThemedButton {
                    width: parent.width
                    variant: "secondary"
                    glyph: Theme.icons.copy
                    text: qsTr("Copy ChatGPT address")
                    enabled: EditorState.mcp.chatGptUrl(root.chatGptAddress).length > 0
                    tooltip: qsTr("Needs the https:// address from step 3")
                    onClicked: {
                        if (EditorState.mcp.copyChatGptUrl(root.chatGptAddress))
                            Toasts.success(qsTr("Copied ChatGPT address"))
                    }
                }
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Paste that into the assistant. To help it use this editor, copy the how-to next and paste it into the chat.")
        }

        ThemedButton {
            width: parent.width
            variant: "ghost"
            glyph: Theme.icons.copy
            text: qsTr("Copy a how-to for the agent")
            tooltip: qsTr("A short list of what the agent can do here — paste it into the chat")
            onClicked: {
                EditorState.mcp.copyAgentGuide()
                Toasts.success(qsTr("Copied how-to"))
            }
        }

        ThemedLabel {
            width: parent.width
            wrapMode: Text.WordWrap
            text: qsTr("Model: most edits here are small, so a mid-size model is enough and a top model only drains usage faster. Dafin cannot change the model of an assistant that is already running; choose it here and start the assistant with the copied command.")
        }

        ThemedComboBox {
            id: modelBox
            width: parent.width
            textRole: "label"
            valueRole: "value"
            model: [
                { label: qsTr("Claude: the app's own default"), value: "" },
                { label: qsTr("Claude Sonnet (most edits)"), value: "sonnet" },
                { label: qsTr("Claude Opus (hard jobs only)"), value: "opus" },
                { label: qsTr("Claude Haiku (smallest)"), value: "haiku" }
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

        ThemedButton {
            width: parent.width
            variant: "ghost"
            glyph: Theme.icons.copy
            text: qsTr("Copy Claude launch command")
            onClicked: {
                EditorState.mcp.copyClaudeLaunchCommand()
                Toasts.success(qsTr("Copied: %1").arg(EditorState.mcp.assistantModel.length > 0
                                                       ? "claude --model " + EditorState.mcp.assistantModel : "claude"))
            }
        }

        ThemedTextField {
            width: parent.width
            placeholderText: qsTr("Codex model name (empty = Codex's own default)")
            text: EditorState.mcp.codexModel
            onEditingFinished: EditorState.mcp.codexModel = text
        }

        ThemedButton {
            width: parent.width
            variant: "ghost"
            glyph: Theme.icons.copy
            text: qsTr("Copy Codex launch command")
            onClicked: {
                EditorState.mcp.copyCodexLaunchCommand()
                Toasts.success(qsTr("Copied launch command"))
            }
        }

        ThemedButton {
            variant: "ghost"
            glyph: root.detailsOpen ? Theme.icons.chevronDown : Theme.icons.chevronRight
            text: qsTr("More options")
            onClicked: root.detailsOpen = !root.detailsOpen
        }

        Column {
            width: parent.width
            spacing: Theme.spacingMd
            visible: root.detailsOpen

            ThemedLabel {
                width: parent.width
                wrapMode: Text.WordWrap
                text: qsTr("For a different assistant, copy a one-time setup. The address and key are already in the Cursor and Claude copies above.")
            }

            ThemedButton {
                width: parent.width
                variant: "ghost"
                glyph: Theme.icons.copy
                text: qsTr("Copy one-time setup")
                tooltip: qsTr("Add this once to the assistant’s config. Access still has to be turned on here.")
                onClicked: {
                    EditorState.mcp.copyStdioSnippet()
                    Toasts.success(qsTr("Copied one-time setup"))
                }
            }
        }
    }
}
