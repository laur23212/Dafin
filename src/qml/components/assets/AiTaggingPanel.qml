import QtQuick
import QtQuick.Controls.Basic
import Drift

// Dafin: AI › Media auto-tagging (EditorState.ai.tagging). Looks at the media in the bin in the
// background — what the sound is (the audio tagger) and what is in the picture (a small object
// detector on a few frames) — and stores tags on each item, so the media search finds "dog" or
// "speech". Tags are listed here per item and each can be removed; "Auto-tag new imports" (off by
// default) tags media as it arrives. Everything runs on this computer.
Item {
    id: root

    readonly property var tagging: EditorState.ai.tagging
    property bool hearReady: Addons.hasKind("audio-tag-model") && Addons.runtimeAvailable()
    property bool seeReady: Addons.hasKind("object-model") && Addons.runtimeAvailable()
    readonly property bool anyModel: root.hearReady || root.seeReady

    function refreshModels() {
        root.hearReady = Addons.hasKind("audio-tag-model") && Addons.runtimeAvailable()
        root.seeReady = Addons.hasKind("object-model") && Addons.runtimeAvailable()
        root.tagging.refreshModels()
    }

    Component.onCompleted: root.refreshModels()

    Connections {
        target: Addons
        function onKindChanged(kind) { root.refreshModels() }
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
                text: qsTr("Tags your media by what is heard and seen in it — speech, music, a dog, a car — so the media search finds it. Runs on this computer, in the background.")
                color: Theme.c.subtitlesTab.foregroundMuted
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeXs
            }

            // --- models ------------------------------------------------------------------------
            AiModelDownload {
                id: pictureDownload
                width: column.width
                visible: !root.seeReady && (pictureDownload.known || pictureDownload.busy || !root.anyModel)
                compact: root.anyModel
                addonIds: ["object.yolox-tiny"]
                purpose: qsTr("Tags for pictures and video: people, animals, vehicles and everyday objects.")
                buttonText: qsTr("Download picture tagging")
            }
            AiModelDownload {
                id: soundDownload
                width: column.width
                visible: !root.hearReady && (soundDownload.known || soundDownload.busy || !root.anyModel)
                compact: root.anyModel
                addonIds: ["audio.clap"]
                purpose: qsTr("Tags for sound: speech, music and its style, instruments, ambience.")
                buttonText: qsTr("Download sound tagging")
            }

            // --- run -----------------------------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingSm
                visible: root.anyModel

                ThemedButton {
                    width: column.width
                    visible: !root.tagging.running
                    text: root.tagging.untaggedCount > 0
                          ? qsTr("Tag %n untagged item(s)", "", root.tagging.untaggedCount)
                          : qsTr("Everything is tagged")
                    variant: "primary"
                    glyph: "tags"
                    enabled: root.tagging.untaggedCount > 0
                    onClicked: root.tagging.tagUntagged()
                }
                ThemedButton {
                    width: column.width
                    visible: !root.tagging.running && AssetLibrary.count > 0
                    text: qsTr("Tag everything again")
                    variant: "secondary"
                    glyph: Theme.icons.refresh
                    tooltip: qsTr("Looks at every item again and replaces its tags")
                    onClicked: root.tagging.tagAll()
                }

                Text {
                    width: column.width
                    visible: root.tagging.running || root.tagging.status.length > 0
                    wrapMode: Text.WordWrap
                    text: root.tagging.running && root.tagging.status.length === 0
                          ? qsTr("Tagging… %1%").arg(Math.round(root.tagging.progress * 100))
                          : root.tagging.status
                    color: Theme.c.subtitlesTab.statusForeground
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
                ThemedProgressBar {
                    width: column.width
                    visible: root.tagging.running
                    value: root.tagging.progress
                }
                ThemedButton {
                    visible: root.tagging.running
                    text: qsTr("Cancel")
                    variant: "destructive"
                    glyph: Theme.icons.x
                    tooltip: qsTr("Stop; the items already done keep their tags")
                    onClicked: root.tagging.cancel()
                }

                ThemedSwitch {
                    width: column.width
                    text: qsTr("Auto-tag new imports")
                    checked: root.tagging.autoTagImports
                    tooltip: qsTr("Tag media in the background as it is added to the bin")
                    onToggled: root.tagging.autoTagImports = checked
                }
            }

            // --- what is tagged --------------------------------------------------------------
            Column {
                width: column.width
                spacing: Theme.spacingSm
                visible: root.tagging.taggedMedia.length > 0

                Text {
                    width: column.width
                    wrapMode: Text.WordWrap
                    text: qsTr("Tagged media — search the Media tab for any of these words. × removes a tag.")
                    color: Theme.c.subtitlesTab.foregroundMuted
                    font.family: Theme.fontFamily
                    font.pixelSize: Theme.fontSizeXs
                }

                Repeater {
                    model: root.tagging.taggedMedia
                    delegate: Column {
                        id: item
                        required property var modelData
                        width: column.width
                        spacing: Theme.spacingXs

                        Item {
                            width: item.width
                            height: Theme.controlHeightSm
                            Text {
                                anchors.left: parent.left
                                anchors.right: clearButton.left
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideMiddle
                                text: item.modelData.name
                                color: Theme.c.subtitlesTab.foreground
                                font.family: Theme.fontFamily
                                font.pixelSize: Theme.fontSizeXs
                                font.weight: Font.DemiBold
                            }
                            IconButton {
                                id: clearButton
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Theme.icons.x
                                tooltip: qsTr("Remove all tags of “%1”").arg(item.modelData.name)
                                Accessible.name: tooltip
                                onClicked: root.tagging.clearTags([item.modelData.assetId])
                            }
                        }

                        Flow {
                            width: item.width
                            spacing: Theme.spacingXs
                            Repeater {
                                model: item.modelData.tags
                                delegate: Rectangle {
                                    id: chip
                                    required property string modelData
                                    height: 22
                                    width: chipLabel.implicitWidth + chipX.width + Theme.spacingSm + 2
                                    radius: height / 2
                                    color: "transparent"
                                    border.width: 1
                                    border.color: Theme.c.subtitlesTab.accent

                                    Text {
                                        id: chipLabel
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.spacingSm
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: chip.modelData
                                        color: Theme.c.subtitlesTab.foreground
                                        font.family: Theme.fontFamily
                                        font.pixelSize: Theme.fontSizeXs
                                    }
                                    IconButton {
                                        id: chipX
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        buttonSize: 20
                                        iconSize: 12
                                        glyph: Theme.icons.x
                                        tooltip: qsTr("Remove the tag “%1”").arg(chip.modelData)
                                        Accessible.name: tooltip
                                        onClicked: root.tagging.removeTag(item.modelData.assetId, chip.modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
