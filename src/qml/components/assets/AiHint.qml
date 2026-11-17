import QtQuick
import Drift

// Dafin: a muted, wrapping line in an AI tool view: what the tool does, or what to do first
// ("Select a video clip…") so a button is never just dead. warning tints it.
Text {
    property bool warning: false

    width: parent ? parent.width : implicitWidth
    wrapMode: Text.WordWrap
    color: warning ? Theme.c.scenesTab.warning : Theme.c.subtitlesTab.foregroundMuted
    font.family: Theme.fontFamily
    font.pixelSize: Theme.fontSizeXs
}
