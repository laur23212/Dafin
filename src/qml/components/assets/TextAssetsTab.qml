import QtQuick
import QtQuick.Controls.Basic
import Drift
// Text tab: click a style pack to drop a styled text clip on the timeline
// (placeholder copy + inline edit). Timed captions live under Subtitles.
// Templates the user saved from the properties Text tab sit behind the "My templates" chip,
// sorted into their own categories; template files installed from outside sit under "Installed".
Item {
    id: root

    // A clip landed on the timeline. The phone shell closes the sheet on this —
    // the thing you came for is behind it.
    signal added()

    property string section: "builtin"
    // Dafin: My templates filter: "" = all, "__none__" = uncategorised, else a category id.
    property string category: ""

    // A QVariantList from an invokable is not reactive, so each list is refreshed by poking a
    // counter from the controller's signal.
    property int templatesTick: 0
    readonly property var presets: {
        void root.templatesTick
        return EditorState.textTemplates()
    }
    readonly property var builtinPresets: presets.filter(p => p.origin !== "installed" && p.origin !== "linked")
    readonly property var installedPresets: presets.filter(p => p.origin === "installed")
    // Dafin: Scene Titles (docs/scene-titles.md), built-in ports and installed files.
    property int scenesTick: 0
    readonly property var scenes: scenesTick >= 0 ? EditorState.sceneTitles() : []
    // Dafin: Titles › Keyboard Shortcuts, the Smart Keycaps scenes (docs/scene-keycaps.md), apart from
    // Titles › Scenes. The four approved titles sit next to the template of the same layout.
    readonly property string shortcutCategory: "Keyboard Shortcuts"
    readonly property var otherScenes: scenes.filter(s => s.category !== shortcutCategory)
    readonly property var shortcutScenes: scenes.filter(s => s.category === shortcutCategory)
                                                .map((s, i) => ({ "scene": s, "rank": shortcutRank(s.id), "i": i }))
                                                .sort((a, b) => a.rank - b.rank || a.i - b.i)
                                                .map(x => x.scene)
    function shortcutRank(id) {
        const approved = { "dafin.shortcut.13a-shortcut-keys": 35,                 // before T04 Multiple Modifiers
                           "dafin.shortcut.13c-u3-mac-dual-names-force-quit": 55,  // before T06 Dual Key Names
                           "dafin.shortcut.13c-u1-universal-horizontal-copy": 75,  // before T08 Side by Side
                           "dafin.shortcut.13c-u2-universal-vertical-paste": 85 }  // before T09 Stacked
        if (approved[id] !== undefined)
            return approved[id]
        const m = /^dafin\.keyboard-shortcuts\.t(\d\d)-/.exec(id)
        return m ? Number(m[1]) * 10 : 1000
    }
    // Dafin: Filmora titles converted from linked folders (Settings › Asset Providers).
    readonly property var linkedPresets: presets.filter(p => p.origin === "linked")

    property int userPresetsTick: 0
    readonly property var allUserPresets: {
        void root.userPresetsTick
        return EditorState.userTextPresets()
    }
    readonly property var userPresets: allUserPresets.filter(p => root.category === ""
                                                                  || (root.category === "__none__" ? !p.category
                                                                                                   : p.category === root.category))
    property int categoriesTick: 0
    readonly property var categories: {
        void root.categoriesTick
        void root.userPresetsTick
        return EditorState.templateCategories()
    }
    // Dafin: the My area (favorites and the user's groups) covers every title card: built-in,
    // installed and the user's own templates. section is then "__favorites__" or a group id.
    readonly property var allTitles: builtinPresets.concat(linkedPresets).map(p => Object.assign({ "_titleKind": "builtin" }, p))
                                     .concat(installedPresets.map(p => Object.assign({ "_titleKind": "installed" }, p)))
                                     .concat(allUserPresets.map(p => Object.assign({ "_titleKind": "user" }, p)))
    readonly property bool showsMyList: AssetGroups.isMyList(root.section)
    readonly property var myTitles: showsMyList ? AssetGroups.pick("titles", root.section, root.allTitles, p => p.id) : []
    readonly property real topArea: myBar.y + myBar.height

    // Dafin: search by name across every title (built-in, installed, mine). While there is
    // a query, the section chips' grids give way to one result grid and the My bar is hidden.
    property alias searchText: titleSearch.text
    readonly property string query: titleSearch.text.trim().toLowerCase()
    readonly property bool searching: query.length > 0
    readonly property var searchResults: {
        const q = root.query
        if (q.length === 0)
            return []
        return root.allTitles.filter(p => {
            const hay = [p.label, p.name, p.id, p.category, p.description,
                         Array.isArray(p.tags) ? p.tags.join(" ") : p.tags]
                .filter(x => typeof x === "string").join(" ").toLowerCase()
            return hay.indexOf(q) >= 0
        })
    }
    // What the shared card grid shows: search results, else Favorites / Recently used / a group.
    readonly property var shownTitles: searching ? searchResults : myTitles

    readonly property var activeCategory: {
        for (const c of root.categories)
            if (c.id === root.category)
                return c
        return null
    }

    Connections {
        target: EditorState
        function onUserTextPresetsChanged() { root.userPresetsTick++ }
        function onTextTemplatesChanged() { root.templatesTick++ }
        function onTemplateCategoriesChanged() { root.categoriesTick++ }
        function onSceneTitlesChanged() { root.scenesTick++ }
    }

    // Titles from linked folders arrive after a scan, without the editor being told.
    Connections {
        target: Addons
        function onKindChanged(kind) {
            if (kind === "text-styles")
                root.templatesTick++
        }
    }

    readonly property string styleFileFilter: qsTr("Dafin template (*.drifttextstyle)")
    readonly property string templateFileFilter: qsTr("Dafin title template (*.json)")

    function importStyle() {
        const url = FileDialogs.openFile(qsTr("Import a saved template"), [root.styleFileFilter])
        if (url.toString().length > 0) {
            EditorState.importUserTextPreset(url)
            root.section = "mine"
        }
    }

    // A template file from outside Dafin (for example made by ChatGPT): checked first; problems
    // are shown with a report the user can paste back to its author word for word.
    function installTemplate() {
        const url = FileDialogs.openFile(qsTr("Install a title template"), [root.templateFileFilter])
        if (url.toString().length === 0)
            return
        const result = EditorState.installTextTemplate(url)
        if (result.ok === true)
            root.section = "installed"
        if (result.ok !== true || (result.issues || []).length > 0)
            reportDialog.openWith(result)
    }

    // Dafin: a Scene Title file (format "dafin.scene-title"), checked with its fonts first.
    readonly property string sceneFileFilter: qsTr("Dafin scene title (*.json)")
    function installScene() {
        const url = FileDialogs.openFile(qsTr("Install a scene title"), [root.sceneFileFilter])
        if (url.toString().length === 0)
            return
        const result = EditorState.installSceneTitle(url)
        if (result.ok === true) {
            const installed = EditorState.sceneTitles().find(s => s.id === result.id)
            root.section = installed && installed.category === root.shortcutCategory ? "shortcuts" : "scenes"
        } else
            reportDialog.openWith(result)
    }

    function exportStyle(preset) {
        const url = FileDialogs.saveFile(qsTr("Export text style"), [root.styleFileFilter],
                                         preset.label, "drifttextstyle")
        if (url.toString().length > 0)
            EditorState.exportUserTextPreset(preset.id, url)
    }

    Item {
        id: toolbar
        width: parent.width
        height: Theme.pagePadding + Theme.controlHeight

        Row {
            x: Theme.pagePadding
            y: Theme.pagePadding
            height: Theme.controlHeight
            spacing: Theme.spacingSm

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Built-in")
                variant: "outline"
                selected: root.section === "builtin"
                onClicked: root.section = "builtin"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.linkedPresets.length > 0
                text: qsTr("Filmora · %1").arg(root.linkedPresets.length)
                variant: "outline"
                selected: root.section === "linked"
                onClicked: root.section = "linked"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.installedPresets.length > 0
                text: qsTr("Installed · %1").arg(root.installedPresets.length)
                variant: "outline"
                selected: root.section === "installed"
                onClicked: root.section = "installed"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.otherScenes.length > 0
                text: qsTr("Scenes · %1").arg(root.otherScenes.length)
                variant: "outline"
                selected: root.section === "scenes"
                onClicked: root.section = "scenes"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                visible: root.shortcutScenes.length > 0
                text: qsTr("Keyboard Shortcuts · %1").arg(root.shortcutScenes.length)
                variant: "outline"
                selected: root.section === "shortcuts"
                onClicked: root.section = "shortcuts"
            }

            ThemedChip {
                anchors.verticalCenter: parent.verticalCenter
                text: root.allUserPresets.length > 0 ? qsTr("My templates · %1").arg(root.allUserPresets.length)
                                                     : qsTr("My templates")
                variant: "outline"
                selected: root.section === "mine"
                onClicked: root.section = "mine"
            }
        }

        IconButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.pagePadding
            y: Theme.pagePadding
            buttonSize: Theme.controlHeight
            glyph: Theme.icons.folderInput
            variant: "ghost"
            tooltip: qsTr("Install or import a template…")
            onClicked: importMenu.popup()
        }

        ThemedContextMenu {
            id: importMenu
            ThemedMenuItem {
                text: qsTr("Install a title template (.json)…")
                icon.name: Theme.icons.folderInput
                onTriggered: root.installTemplate()
            }
            ThemedMenuItem {
                text: qsTr("Install a scene title (.json)…")
                icon.name: Theme.icons.folderInput
                onTriggered: root.installScene()
            }
            ThemedMenuItem {
                text: qsTr("Import a saved template (.drifttextstyle)…")
                icon.name: Theme.icons.folderInput
                onTriggered: root.importStyle()
            }
        }
    }

    // Dafin: My templates by category. All · Uncategorized · the user's categories · + new.
    Item {
        id: categoryBar
        width: parent.width
        y: root.topArea + Theme.spacingSm
        height: visible ? Theme.controlHeightSm + Theme.spacingSm : 0
        visible: root.section === "mine" && !root.searching

        Flickable {
            x: Theme.pagePadding
            width: parent.width - Theme.pagePadding * 2
            height: Theme.controlHeightSm
            contentWidth: categoryRow.width
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: categoryRow
                height: parent.height
                spacing: Theme.spacingXs

                ThemedChip {
                    text: qsTr("All")
                    variant: "outline"
                    selected: root.category === ""
                    onClicked: root.category = ""
                }
                ThemedChip {
                    text: qsTr("Uncategorized")
                    variant: "outline"
                    selected: root.category === "__none__"
                    onClicked: root.category = "__none__"
                }
                Repeater {
                    model: root.categories
                    delegate: ThemedChip {
                        required property var modelData
                        text: modelData.count > 0 ? qsTr("%1 · %2").arg(modelData.name).arg(modelData.count) : modelData.name
                        variant: "outline"
                        selected: root.category === modelData.id
                        onClicked: root.category = modelData.id
                    }
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    buttonSize: Theme.controlHeightSm
                    glyph: Theme.icons.plus
                    variant: "ghost"
                    tooltip: qsTr("New category…")
                    onClicked: newCategoryDialog.openWith(qsTr("New category"), "")
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.activeCategory !== null
                    buttonSize: Theme.controlHeightSm
                    glyph: Theme.icons.ellipsis
                    variant: "ghost"
                    tooltip: qsTr("Category options")
                    onClicked: categoryMenu.popup()
                }
            }
        }

        ThemedContextMenu {
            id: categoryMenu
            ThemedMenuItem {
                text: qsTr("Rename category…")
                icon.name: Theme.icons.pencil
                onTriggered: renameCategoryDialog.openWith(qsTr("Rename category"), root.activeCategory ? root.activeCategory.name : "")
            }
            ThemedMenuItem {
                text: qsTr("Delete category…")
                icon.name: Theme.icons.trash
                onTriggered: deleteCategoryDialog.openFor(root.activeCategory)
            }
        }
    }

    // Dafin: ⭐ Favorites, the user's groups and + New group, under the section chips.
    AssetSearchBar {
        id: titleSearch
        y: toolbar.height
        placeholderText: qsTr("Search titles")
    }

    AssetCategoryChips {
        id: myBar
        y: titleSearch.y + titleSearch.height
        searching: root.searching
        width: parent.width
        tabId: "titles"
        activeCategory: root.section
        onCategoryActivated: (categoryId) => root.section = categoryId
    }

    // Dafin: the card label's height, for the grid rows. It was used below but never defined, so
    // every cellHeight binding failed and the rows stayed at the 100 px default while the cards are
    // taller: each card covered the lower half of the one above it, cutting off its preview.
    FontMetrics {
        id: labelMetrics
        font.family: Theme.fontFamily
        font.pixelSize: Theme.fontSizeXs
    }

    // Built-in packs and the user's own styles are separate grids, switched from the toolbar.
    GridView {
        id: packGrid
        // As many columns as fit at the nominal card size, rounded, with the cards stretched or
        // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
        // Dafin: two large previews per row.
        readonly property int columnCount: 2
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: root.topArea + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: (root.section === "builtin" || root.section === "linked") && !root.searching
        model: root.section === "linked" ? root.linkedPresets : root.builtinPresets

        delegate: TextStylePackCard {
            cardSize: packGrid.cardSize
            onAdded: root.added()
        }
    }

    GridView {
        id: userGrid
        // As many columns as fit at the nominal card size, rounded, with the cards stretched or
        // squeezed a little to fill the row — a fixed card left a column's worth of empty space.
        // Dafin: two large previews per row.
        readonly property int columnCount: 2
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: categoryBar.y + categoryBar.height
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: root.section === "mine" && !root.searching && root.userPresets.length > 0
        model: root.userPresets

        delegate: TextStyleUserCard {
            cardSize: userGrid.cardSize
            onAdded: root.added()
            onExportRequested: (preset) => root.exportStyle(preset)
            onRenameRequested: (preset) => renameDialog.openFor(preset)
            onDeleteRequested: (preset) => deleteDialog.openFor(preset)
            onDetailsRequested: (preset) => detailsDialog.openForEdit(preset)
            onDuplicateRequested: (preset) => EditorState.duplicateUserTextPreset(preset.id)
        }
    }

    // Templates installed from files (an external author's work): same cards, remove instead of edit.
    GridView {
        id: installedGrid
        // Dafin: two large previews per row.
        readonly property int columnCount: 2
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: root.topArea + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: root.section === "installed" && !root.searching
        model: root.installedPresets

        delegate: TextStyleUserCard {
            cardSize: installedGrid.cardSize
            installed: true
            onAdded: root.added()
            onDeleteRequested: (preset) => EditorState.uninstallTextTemplate(preset.id)
        }
    }

    // Dafin: Titles › Scenes and Titles › Keyboard Shortcuts. One card per Scene Title; each lands as one clip.
    GridView {
        id: sceneGrid
        readonly property int columnCount: 2
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: root.topArea + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.5625) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: (root.section === "scenes" || root.section === "shortcuts") && !root.searching
        model: root.section === "shortcuts" ? root.shortcutScenes : root.otherScenes

        delegate: SceneTitleCard {
            cardSize: sceneGrid.cardSize
            onAdded: root.added()
            onDeleteRequested: (scene) => EditorState.uninstallSceneTitle(scene.id)
        }
    }

    // Dafin: favorites or one of the user's groups, in the user's order; any kind of title card.
    GridView {
        id: myGrid
        readonly property int columnCount: 2
        readonly property real cardSize: Math.floor(width / columnCount) - Theme.assetCardGap
        x: Theme.pagePadding
        y: root.topArea + Theme.spacingSm
        width: parent.width - Theme.pagePadding * 2 + Theme.assetCardGap
        height: parent.height - y
        bottomMargin: Theme.spacing3xl
        clip: true
        reuseItems: true
        boundsBehavior: Flickable.StopAtBounds
        acceptedButtons: Theme.touchUi ? Qt.LeftButton : Qt.NoButton
        ScrollBar.vertical: AppScrollBar { }
        cellWidth: Math.floor(width / columnCount)
        cellHeight: Math.round(cardSize * 0.55) + Theme.spacingSm
                    + Math.ceil(labelMetrics.height) + Theme.assetCardGap
        visible: (root.showsMyList || root.searching) && root.shownTitles.length > 0
        model: root.shownTitles

        delegate: Item {
            id: myCell
            required property var modelData
            width: myGrid.cellWidth
            height: myGrid.cellHeight

            Loader {
                sourceComponent: myCell.modelData._titleKind === "builtin" ? myPackCard : myUserCard
            }

            Component {
                id: myPackCard
                TextStylePackCard {
                    modelData: myCell.modelData
                    cardSize: myGrid.cardSize
                    onAdded: root.added()
                }
            }

            Component {
                id: myUserCard
                TextStyleUserCard {
                    modelData: myCell.modelData
                    cardSize: myGrid.cardSize
                    installed: myCell.modelData._titleKind === "installed"
                    onAdded: root.added()
                    onExportRequested: (preset) => root.exportStyle(preset)
                    onRenameRequested: (preset) => renameDialog.openFor(preset)
                    onDeleteRequested: (preset) => myCell.modelData._titleKind === "installed"
                                                   ? EditorState.uninstallTextTemplate(preset.id)
                                                   : deleteDialog.openFor(preset)
                    onDetailsRequested: (preset) => detailsDialog.openForEdit(preset)
                    onDuplicateRequested: (preset) => EditorState.duplicateUserTextPreset(preset.id)
                }
            }
        }
    }

    // Dafin: drag a title onto another's place to reorder favorites and groups.
    AssetReorderArea {
        anchors.fill: myGrid
        view: myGrid
        tabId: "titles"
        listId: root.searching ? "" : root.section
        keyAt: i => root.shownTitles[i] ? root.shownTitles[i].id : ""
    }

    EmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.topArea + Math.max(0, (parent.height - root.topArea - height) / 2)
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        visible: (root.showsMyList || root.searching) && root.shownTitles.length === 0
        compact: true
        glyph: root.searching ? Theme.icons.search : Theme.icons.star
        title: root.searching ? qsTr("No titles match “%1”").arg(titleSearch.text.trim())
               : root.section === AssetGroups.recentId ? qsTr("No recently used titles")
               : root.section === AssetGroups.favoritesId ? qsTr("No favorite titles yet") : qsTr("This group is empty")
        hint: root.searching ? qsTr("Try a different name.")
              : root.section === AssetGroups.recentId ? qsTr("Titles you add show up here.")
              : root.section === AssetGroups.favoritesId
              ? qsTr("Star a title to keep it here.")
              : qsTr("Right-click a title and choose Add to group, or drop it on the group.")
    }

    EmptyState {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.topArea + Math.max(0, (parent.height - root.topArea - height) / 2)
        width: Math.min(parent.width - Theme.spacing3xl, 260)
        visible: root.section === "mine" && !root.searching && root.userPresets.length === 0
        compact: true
        glyph: Theme.icons.type
        title: root.category === "" ? qsTr("No saved templates") : qsTr("Nothing in this category yet")
        hint: qsTr("Style a title, then use “Save as new template…” in its properties to keep it here.")
        actionText: qsTr("Import")
        onActionTriggered: root.importStyle()
    }

    NameDialog {
        id: renameDialog

        property string presetId: ""

        function openFor(preset) {
            presetId = preset.id
            openWith(qsTr("Rename template"), preset.label)
        }

        onSubmitted: name => EditorState.renameUserTextPreset(renameDialog.presetId, name)
    }

    SaveTemplateDialog {
        id: detailsDialog
    }

    NameDialog {
        id: newCategoryDialog
        onSubmitted: name => {
            const id = EditorState.addTemplateCategory(name)
            if (id.length > 0)
                root.category = id
        }
    }

    NameDialog {
        id: renameCategoryDialog
        onSubmitted: name => {
            if (root.activeCategory)
                EditorState.renameTemplateCategory(root.activeCategory.id, name)
        }
    }

    // Deleting a category never deletes templates: they move to another category or to Uncategorized.
    ThemedDialog {
        id: deleteCategoryDialog
        property var target: null
        readonly property var others: target ? root.categories.filter(c => c.id !== target.id) : []
        title: qsTr("Delete category")
        acceptText: qsTr("Delete category")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(category) {
            if (!category)
                return
            target = category
            moveBox.currentIndex = 0
            open()
        }

        onAccepted: {
            const i = moveBox.currentIndex - 1
            const moveTo = i >= 0 && i < others.length ? others[i].id : ""
            EditorState.removeTemplateCategory(target.id, moveTo)
            root.category = moveTo.length > 0 ? moveTo : ""
        }

        contentItem: Column {
            width: parent ? parent.width : 320
            spacing: Theme.spacingSm
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: deleteCategoryDialog.target
                      ? qsTr("Delete “%1”? Its %n template(s) are kept and moved to:", "", deleteCategoryDialog.target.count)
                            .arg(deleteCategoryDialog.target.name)
                      : ""
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            ThemedComboBox {
                id: moveBox
                width: parent.width
                model: [qsTr("Uncategorized")].concat(deleteCategoryDialog.others.map(c => c.name))
            }
        }
    }

    // What the template check found, with the plain-text report ready to paste back to the
    // template's author (ChatGPT, a designer) without retelling it.
    ThemedDialog {
        id: reportDialog
        // Not "result": Dialog.result is FINAL in Qt 6.10, and overriding it failed the whole UI.
        property var checkResult: ({})
        title: checkResult.ok === true ? qsTr("Template installed") : qsTr("This template can't be installed yet")
        acceptText: qsTr("Copy report")
        rejectText: qsTr("Close")
        preferredWidth: Theme.dialogWidthMd
        acceptOnReturn: false

        function openWith(r) {
            checkResult = r
            open()
        }

        onAccepted: EditorState.copyPlainTextToClipboard(checkResult.report || "")

        contentItem: Column {
            width: parent ? parent.width : 420
            spacing: Theme.spacingSm
            Text {
                width: parent.width
                wrapMode: Text.WordWrap
                text: reportDialog.checkResult.ok === true
                      ? qsTr("“%1” is in Installed. Notes from the check:").arg(reportDialog.checkResult.label || "")
                      : qsTr("Copy the report and send it to whoever made the template, so they can fix it:")
                color: Theme.panelForeground
                font.family: Theme.fontFamily
                font.pixelSize: Theme.fontSizeSm
            }
            Flickable {
                width: parent.width
                height: Math.min(260, reportText.implicitHeight)
                contentHeight: reportText.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar { }
                TextEdit {
                    id: reportText
                    width: parent.width
                    readOnly: true
                    selectByMouse: true
                    wrapMode: Text.Wrap
                    text: reportDialog.checkResult.report || ""
                    color: Theme.mutedForeground
                    font.family: Theme.monoFontFamily
                    font.pixelSize: Theme.fontSizeXs
                }
            }
        }
    }

    ThemedDialog {
        id: deleteDialog

        property string presetId: ""
        property string presetLabel: ""

        title: qsTr("Delete template")
        acceptText: qsTr("Delete")
        acceptVariant: "destructive"
        preferredWidth: Theme.dialogWidthSm
        acceptOnReturn: false

        function openFor(preset) {
            presetId = preset.id
            presetLabel = preset.label
            open()
        }

        onAccepted: EditorState.deleteUserTextPreset(deleteDialog.presetId)

        contentItem: Text {
            width: parent ? parent.width : 320
            wrapMode: Text.WordWrap
            text: qsTr("Remove “%1” from My templates? Titles already using it keep their look.")
                      .arg(deleteDialog.presetLabel)
            color: Theme.c.textAssetsTab.foreground
            font.family: Theme.fontFamily
            font.pixelSize: Theme.fontSizeSm
        }
    }
}
