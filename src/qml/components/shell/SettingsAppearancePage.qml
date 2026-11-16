import QtQuick
import QtQuick.Window
import Drift

SettingsPage {
    id: page

    pageId: "appearance"
    title: qsTr("Appearance")

    function chooseTheme(themeId) {
        if (themeId === "dark" || themeId === "light")
            EditorState.preferences.setDarkModePreference(themeId === "dark")
        if (themeId !== "system" && themeId !== "dark" && themeId !== "light") {
            const themes = ThemeManager.themes
            for (let i = 0; i < themes.length; ++i) {
                const theme = themes[i]
                if (theme.id === themeId && theme.hasOverrides && !theme.trusted) {
                    trustDialog.prompt(themeId)
                    return
                }
            }
        }
        page.applyTheme(themeId)
    }

    function applyTheme(themeId) {
        if (!ThemeManager.select(themeId))
            Toasts.error(ThemeManager.lastError || qsTr("Could not use that theme."))
        else if (ThemeManager.lastWarning)
            Toasts.warning(ThemeManager.lastWarning)
    }

    readonly property bool activeHasVariants: {
        const themes = ThemeManager.themes
        for (let i = 0; i < themes.length; ++i) {
            if (themes[i].id === ThemeManager.selection)
                return !!themes[i].hasVariants
        }
        return false
    }

    // Dafin: one search for both grids; a theme copied into the folder by hand shows up
    // when this page opens.
    property string themeFilter: ""
    Component.onCompleted: ThemeManager.refreshThemes()

    readonly property var selectedRow: {
        const themes = ThemeManager.themes
        for (let i = 0; i < themes.length; ++i) {
            if (themes[i].id === ThemeManager.selection)
                return themes[i]
        }
        return null
    }

    SettingsGroupHeader {
        text: qsTr("Color themes")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Choose a color theme")
        searchId: "appearance-color-theme"
        description: qsTr("Colors, corners and spacing. System follows your light or dark setting.")
        keywords: "dark light color theme palette"
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            ThemedTextField {
                width: Math.min(parent.width, 320)
                placeholderText: qsTr("Search themes…")
                text: page.themeFilter
                onTextChanged: page.themeFilter = text
            }

            SettingsThemeList {
                kind: "color"
                filter: page.themeFilter
                onThemeChosen: (themeId) => page.chooseTheme(themeId)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("QML themes")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Choose a QML theme")
        searchId: "appearance-qml-theme"
        description: qsTr("Skins that also reshape buttons, panels, sliders and switches. The ones that come with Dafin are ready to use; an imported one asks before its QML runs.")
        keywords: "qml skin theme shape retro"
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            SettingsThemeList {
                kind: "qml"
                filter: page.themeFilter
                onThemeChosen: (themeId) => page.chooseTheme(themeId)
            }

            ThemedButton {
                visible: !!page.selectedRow && !!page.selectedRow.removable
                         && !!page.selectedRow.hasOverrides && !!page.selectedRow.trusted
                variant: "ghost"
                text: qsTr("Stop running this theme's QML")
                onClicked: ThemeManager.setThemeTrusted(ThemeManager.selection, false)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Customize")
        query: page.query
    }

    SettingsRow {
        stacked: true
        title: qsTr("Accent color")
        searchId: "appearance-accent-color"
        description: qsTr("Laid over any theme: buttons, selections, sliders and the focus ring. QML themes keep the colors drawn into their own shapes.")
        keywords: "accent color colour highlight primary"
        query: page.query

        SettingsAccentPicker { }
    }

    SettingsRow {
        stacked: true
        title: qsTr("Your themes")
        searchId: "appearance-theme-files"
        description: qsTr("Import a theme file, folder or .zip, export the current theme, or remove one you imported.")
        keywords: "import export remove theme folder"
        query: page.query

        Column {
            width: parent.width
            spacing: Theme.spacingLg

            ThemedLabel {
                width: parent.width
                visible: ThemeManager.appearanceFollowsTheme
                wrapMode: Text.WordWrap
                color: Theme.c.settingsRow.foreground
                text: qsTr("This theme is %1 only. Light and Dark leave it and return to the system theme.")
                      .arg(ThemeManager.effectiveVariant === "dark" ? qsTr("dark") : qsTr("light"))
            }

            ThemedLabel {
                width: parent.width
                visible: page.activeHasVariants
                wrapMode: Text.WordWrap
                color: Theme.c.settingsRow.foreground
                text: qsTr("This theme has a dark and a light variant. The header toggle switches between them.")
            }

            SettingsAppearanceActions {
                onImported: (themeId) => page.chooseTheme(themeId)
            }
        }
    }

    SettingsRow {
        stacked: true
        title: qsTr("Translucent windows")
        searchId: "appearance-translucent-windows"
        description: qsTr("Lets the desktop show through panels when a theme asks for it. Takes effect after restart.")
        keywords: "transparent backdrop blur"
        query: page.query

        SettingsAppearanceTranslucency { }
    }

    // Popups are not column children. The dialog reparents itself to the overlay.
    Item {
        visible: false
        ThemeTrustDialog {
            id: trustDialog

            onTrustChosen: {
                ThemeManager.setThemeTrusted(trustDialog.themeId, true)
                page.applyTheme(trustDialog.themeId)
            }
            onSkipChosen: {
                page.applyTheme(trustDialog.themeId)
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Interface")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Interface size")
        searchId: "appearance-interface-size"
        description: EditorState.preferences.uiScaleNeedsRestart
                     ? qsTr("Restart Drift to apply this size.")
                     : Qt.platform.os === "windows"
                       ? qsTr("100% fits a 1080p screen, whatever your Windows display scale is. Ctrl+Plus, Ctrl+Shift+Minus and Ctrl+0 change it anywhere.")
                       : qsTr("Makes buttons, text, and icons larger, on top of your display settings.")
        keywords: "scale zoom dpi"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: 0.8,  label: "80%" },
                { id: 0.9,  label: "90%" },
                { id: 1.0,  label: Qt.platform.os === "windows" ? "100%" : qsTr("100% (system)") },
                { id: 1.1,  label: "110%" },
                { id: 1.25, label: "125%" },
                { id: 1.5,  label: "150%" }
            ]
            currentIndex: {
                const opts = model
                for (var i = 0; i < opts.length; ++i) {
                    if (Math.abs(opts[i].id - EditorState.preferences.uiScale) < 0.001)
                        return i
                }
                return 0
            }
            onActivated: {
                if (currentIndex >= 0 && currentIndex < model.length)
                    EditorState.preferences.uiScale = model[currentIndex].id
            }
        }
    }

    SettingsRow {
        title: qsTr("Layout")
        searchId: "appearance-layout"
        description: EditorState.preferences.interfaceShellNeedsRestart
                     ? qsTr("Restart Drift to switch layouts.")
                     : qsTr("Desktop suits a mouse and keyboard. Mobile is the touch layout made for phones.")
        keywords: "desktop mobile shell phone tablet"
        query: page.query

        Column {
            width: Theme.c.settingsRow.controlWidth
            spacing: Theme.spacingMd

            ThemedComboBox {
                width: parent.width
                textRole: "label"
                valueRole: "id"
                model: [
                    { id: "auto",    label: qsTr("Automatic") },
                    { id: "desktop", label: qsTr("Desktop") },
                    { id: "mobile",  label: qsTr("Mobile") }
                ]
                currentIndex: {
                    const opts = model
                    for (var i = 0; i < opts.length; ++i) {
                        if (opts[i].id === EditorState.preferences.interfaceShell)
                            return i
                    }
                    return 0
                }
                onActivated: {
                    if (currentIndex >= 0 && currentIndex < model.length)
                        EditorState.preferences.interfaceShell = model[currentIndex].id
                }
            }

            ThemedButton {
                visible: EditorState.preferences.interfaceShellNeedsRestart
                         && EditorState.preferences.canRestart
                variant: "secondary"
                text: qsTr("Restart now")
                onClicked: ShellBridge.main.restartToApplyShell()
            }
        }
    }

    SettingsGroupHeader {
        text: qsTr("Workspace")
        query: page.query
    }

    SettingsRow {
        title: qsTr("Workspace orientation")
        searchId: "appearance-workspace-orientation"
        description: qsTr("Landscape or portrait workspace. Auto follows the shape of the canvas.")
        keywords: "landscape portrait rotate layout"
        query: page.query

        ThemedComboBox {
            width: Theme.c.settingsRow.controlWidth
            textRole: "label"
            valueRole: "id"
            model: [
                { id: "auto",      label: qsTr("Auto (follow canvas)") },
                { id: "landscape", label: qsTr("Landscape") },
                { id: "portrait",  label: qsTr("Portrait") }
            ]
            currentIndex: {
                if (!EditorState.preferences.workspaceLayoutOverridden)
                    return 0
                return EditorState.preferences.workspaceLayoutPreferred === "portrait" ? 2 : 1
            }
            onActivated: {
                const id = model[currentIndex].id
                if (id === "auto")
                    EditorState.preferences.clearWorkspaceLayoutPreference()
                else
                    EditorState.preferences.setWorkspaceLayoutPreference(id)
            }
        }
    }
}
