import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import Aether
import Quill as Q

Window {
    id: root
    title: "Aether Settings"
    flags: Qt.Dialog | Qt.FramelessWindowHint
    color: "transparent"

    width: dialogWidth
    height: pageContainer.implicitHeight
    minimumWidth: dialogWidth
    minimumHeight: dialogMinHeight

    readonly property int dialogWidth: Math.min(920, (transientParent ? transientParent.width : 920) - 32)
    // A fixed floor (not the live height): the compositor grows the window by
    // raising its height, so binding the minimum to it would lock the window
    // to the new size and it could never be shrunk back down.
    readonly property int dialogMinHeight: 420
    readonly property int dialogRadius: draftRadiusLarge + 6

    function syncHyprlandRounding() {
        fileOps.setHyprlandRounding(root.title, root.dialogRadius)
        fileOps.setHyprlandBorder(root.title, 0)
    }

    onDialogRadiusChanged: {
        if (root.visible)
            syncHyprlandRounding()
    }

    readonly property color sectionBorderColor: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
    readonly property string defaultThemeName: "catppuccin-mocha"
    readonly property string defaultIconThemeName: "Adwaita"
    readonly property string defaultSidebarPosition: "left"
    readonly property int defaultSidebarWidth: 200
    readonly property int defaultSidebarAutoHideWidth: 700
    readonly property int defaultRadiusSmall: 4
    readonly property int defaultRadiusMedium: 8
    readonly property int defaultRadiusLarge: 12
    readonly property bool defaultTransparencyEnabled: true
    readonly property real defaultTransparencyLevel: 1.0
    readonly property bool defaultAnimationsEnabled: true
    readonly property bool defaultExpandTransfersByDefault: false
    readonly property int defaultAnimDurationFast: 100
    readonly property int defaultAnimDuration: 200
    readonly property int defaultAnimDurationSlow: 350
    readonly property string defaultAnimCurveEnter: "OutCubic"
    readonly property string defaultAnimCurveExit: "InCubic"
    readonly property string defaultAnimCurveTransition: "Bezier"
    readonly property bool defaultShowWindowControls: false
    readonly property string defaultWindowButtonLayout: ":minimize,maximize,close"
    readonly property string defaultSortBy: "name"
    readonly property bool defaultSortAscending: true
    readonly property bool defaultRememberSortPerFolder: true

    // Sort column dropdown: parallel label/value arrays.
    readonly property var sortByLabels: ["Name", "Size", "Modified", "Type"]
    readonly property var sortByValues: ["name", "size", "modified", "type"]

    property bool currentShowHidden: false
    property bool currentSidebarVisible: true
    property int currentSidebarWidth: 200

    property var themeOptions: []
    property var fontOptions: []
    property var iconThemeOptions: []
    property var availableThemeValues: []
    property var availableFontValues: []
    property var availableIconThemeValues: []
    property bool optionSourcesPrimed: false
    property bool syncingFromConfig: false
    property bool pendingSettingsDirty: false

    property string draftTheme: config.theme
    property string draftThemeMode: config.themeMode
    property string draftFontFamily: config.fontFamily
    property string draftIconTheme: config.iconTheme
    property bool draftShowHidden: currentShowHidden
    property bool draftHiddenLast: config.hiddenLast
    property bool draftRightClickToEditPath: config.rightClickToEditPath
    property bool draftSingleClickOpensFolders: config.singleClickOpensFolders
    property bool draftVimNavigation: config.vimNavigation
    property bool draftDependencyStartupCheck: config.dependencyStartupCheck
    property bool draftSidebarVisible: currentSidebarVisible
    // Must stay in sync with the quick-access entries in Sidebar.qml.
    readonly property var quickAccessNames: ["Home", "Recents", "Trash", "Network", "Pictures", "Downloads"]
    property var draftHiddenQuickAccess: config.hiddenQuickAccess
    property string draftSidebarPosition: config.sidebarPosition
    property string draftSplitOrientation: config.splitOrientation
    property int draftSidebarWidth: currentSidebarWidth
    property int draftSidebarAutoHideWidth: config.sidebarAutoHideWidth
    property bool draftExpandTransfersByDefault: config.expandTransfersByDefault
    property int draftRadiusSmall: config.radiusSmall
    property int draftRadiusMedium: config.radiusMedium
    property int draftRadiusLarge: config.radiusLarge
    property bool draftTransparencyEnabled: config.transparencyEnabled
    property real draftTransparencyLevel: config.transparencyLevel
    property bool draftAnimationsEnabled: config.animationsEnabled
    property int draftAnimDurationFast: config.animDurationFast
    property int draftAnimDuration: config.animDuration
    property int draftAnimDurationSlow: config.animDurationSlow
    property string draftAnimCurveEnter: config.animCurveEnter
    property string draftAnimCurveExit: config.animCurveExit
    property string draftAnimCurveTransition: config.animCurveTransition

    readonly property var curveOptions: ["OutCubic", "InOutCubic", "InCubic", "OutQuad", "InOutQuad", "OutExpo", "InOutExpo", "OutBack", "Linear", "Bezier"]

    property bool draftShowWindowControls: config.showWindowControls
    property string draftWindowButtonLayout: config.windowButtonLayout

    property string draftSortBy: config.sortBy
    property bool draftSortAscending: config.sortAscending
    property bool draftRememberSortPerFolder: config.rememberSortPerFolder

    // Helpers to decompose the layout string for the UI
    readonly property var _layoutParts: {
        var layout = draftWindowButtonLayout || ":minimize,maximize,close"
        var parts = layout.split(":")
        var leftStr = parts[0] || ""
        var rightStr = parts.length > 1 ? parts[1] : ""
        var allButtons = []
        if (leftStr) allButtons = allButtons.concat(leftStr.split(",").filter(function(s) { return s.trim() !== "" }))
        if (rightStr) allButtons = allButtons.concat(rightStr.split(",").filter(function(s) { return s.trim() !== "" }))
        return {
            side: leftStr && !rightStr ? "left" : "right",
            hasClose: allButtons.indexOf("close") >= 0,
            hasMinimize: allButtons.indexOf("minimize") >= 0,
            hasMaximize: allButtons.indexOf("maximize") >= 0
        }
    }

    function setQuickAccessVisible(name, visible) {
        var rest = draftHiddenQuickAccess.filter(function(n) { return n !== name })
        draftHiddenQuickAccess = visible ? rest : rest.concat(name)
    }

    function rebuildButtonLayout(side, hasClose, hasMinimize, hasMaximize) {
        var buttons = []
        if (hasMinimize) buttons.push("minimize")
        if (hasMaximize) buttons.push("maximize")
        if (hasClose) buttons.push("close")
        var str = buttons.join(",")
        draftWindowButtonLayout = side === "left" ? (str + ":") : (":" + str)
        applySettingsNow()
    }

    signal remoteConnectRequested()
    signal cloudDriveConnectRequested()
    signal keyboardShortcutsRequested()
    signal dependenciesRequested()
    signal closed()

    readonly property string systemFontLabel: "System Default"

    Component { id: paletteSectionIcon; IconSettings {} }
    Component { id: layoutSectionIcon; IconPanelLeft {} }
    Component { id: motionSectionIcon; IconClock {} }
    Component { id: driveSectionIcon; IconCloud {} }
    Component { id: toolsSectionIcon; IconFolder {} }
    Component { id: pluginsSectionIcon; IconPackageOpen {} }

    property int currentSectionIndex: 0
    readonly property bool compactNavigation: dialogWidth < 860
    readonly property var sectionNavItems: [
        { title: "Look & Feel", iconComponent: paletteSectionIcon },
        { title: "Layout", iconComponent: layoutSectionIcon },
        { title: "Motion", iconComponent: motionSectionIcon },
        { title: "Drive", iconComponent: driveSectionIcon },
        { title: "Tools", iconComponent: toolsSectionIcon },
        { title: "Plugins", iconComponent: pluginsSectionIcon }
    ]
    readonly property var sectionItems: [
        { title: "Look & Feel", subtitle: "Theme, typography, icons, and surface styling.", iconComponent: paletteSectionIcon },
        { title: "Layout", subtitle: "Sidebar behavior, file visibility, and toolbar controls.", iconComponent: layoutSectionIcon },
        { title: "Motion", subtitle: "Animation timing and easing across the interface.", iconComponent: motionSectionIcon },
        { title: "Drive", subtitle: "Connect cloud storage and review configured drives.", iconComponent: driveSectionIcon },
        { title: "Tools", subtitle: "Shortcuts, remote locations, and config behavior.", iconComponent: toolsSectionIcon },
        { title: "Plugins", subtitle: "Optional extensions for Aether.", iconComponent: pluginsSectionIcon }
    ]

    function showSection(index) {
        currentSectionIndex = index
        if (sideTabs)
            sideTabs.currentIndex = index
        if (compactSectionNav)
            compactSectionNav.currentIndex = index
        if (contentFlick)
            contentFlick.contentY = 0
    }

    function primeOptionSources() {
        if (optionSourcesPrimed)
            return

        availableThemeValues = config.availableThemes
        availableFontValues = config.availableFonts
        availableIconThemeValues = config.availableIconThemes
        optionSourcesPrimed = true
    }

    function buildOptions(values, currentValue, fallbackValue) {
        var options = []
        for (var i = 0; i < values.length; ++i)
            options.push(values[i])

        var preferredValue = currentValue !== "" ? currentValue : fallbackValue
        if (preferredValue && options.indexOf(preferredValue) === -1)
            options.unshift(preferredValue)

        if (options.length === 0 && fallbackValue)
            options.push(fallbackValue)

        return options
    }

    function buildFontOptions() {
        var options = [systemFontLabel]
        for (var i = 0; i < availableFontValues.length; ++i)
            options.push(availableFontValues[i])

        if (draftFontFamily !== "" && options.indexOf(draftFontFamily) === -1)
            options.push(draftFontFamily)

        return options
    }

    function optionIndex(options, value, fallbackIndex) {
        var index = options.indexOf(value)
        return index >= 0 ? index : fallbackIndex
    }

    // Read from the theme's own background rather than its name. The old
    // check treated every theme except catppuccin-latte as dark, so any other
    // light theme showed this toggle stuck on.
    property string draftLightTheme: ""
    property string draftDarkTheme: ""
    property var lightThemeOptions: []
    property var darkThemeOptions: []

    function setDraftThemeMode(mode) {
        draftThemeMode = mode
        if (mode === "light")
            draftTheme = draftLightTheme
        else if (mode === "dark")
            draftTheme = draftDarkTheme
        else if (mode === "system")
            draftTheme = config.systemPrefersLight ? draftLightTheme : draftDarkTheme
    }

    function isDarkTheme(themeName) {
        return (0.2126 * Theme.base.r + 0.7152 * Theme.base.g
                + 0.0722 * Theme.base.b) < 0.5
    }

    function setDraftTheme(themeName) {
        draftTheme = themeName
        draftThemeMode = "custom"
    }

    function bindAppearancePreview() {
        Theme.radiusSmall = Qt.binding(function() {
            return root.visible ? root.draftRadiusSmall : config.radiusSmall
        })
        Theme.radiusMedium = Qt.binding(function() {
            return root.visible ? root.draftRadiusMedium : config.radiusMedium
        })
        Theme.radiusLarge = Qt.binding(function() {
            return root.visible ? root.draftRadiusLarge : config.radiusLarge
        })
        Theme.transparencyEnabled = Qt.binding(function() {
            return root.visible ? root.draftTransparencyEnabled : config.transparencyEnabled
        })
        Theme.transparencyLevel = Qt.binding(function() {
            return root.visible ? root.draftTransparencyLevel : Math.max(0, Math.min(1, config.transparencyLevel))
        })
        Theme.animationsEnabled = Qt.binding(function() {
            return root.visible ? root.draftAnimationsEnabled : config.animationsEnabled
        })
    }

    function resetToDefaults() {
        setDraftTheme(defaultThemeName)
        draftThemeMode = "system"
        draftLightTheme = "catppuccin-latte"
        draftDarkTheme = defaultThemeName
        draftFontFamily = ""
        draftIconTheme = defaultIconThemeName
        draftShowHidden = false
        draftHiddenLast = false
        draftRightClickToEditPath = true
        draftSingleClickOpensFolders = false
        draftVimNavigation = false
        draftDependencyStartupCheck = true
        draftSidebarVisible = true
        draftHiddenQuickAccess = []
        draftSidebarPosition = defaultSidebarPosition
        draftSplitOrientation = "vertical"
        draftSidebarWidth = defaultSidebarWidth
        draftSidebarAutoHideWidth = defaultSidebarAutoHideWidth
        draftExpandTransfersByDefault = defaultExpandTransfersByDefault
        draftRadiusSmall = defaultRadiusSmall
        draftRadiusMedium = defaultRadiusMedium
        draftRadiusLarge = defaultRadiusLarge
        draftTransparencyEnabled = defaultTransparencyEnabled
        draftTransparencyLevel = defaultTransparencyLevel
        draftAnimationsEnabled = defaultAnimationsEnabled
        draftAnimDurationFast = defaultAnimDurationFast
        draftAnimDuration = defaultAnimDuration
        draftAnimDurationSlow = defaultAnimDurationSlow
        draftAnimCurveEnter = defaultAnimCurveEnter
        draftAnimCurveExit = defaultAnimCurveExit
        draftAnimCurveTransition = defaultAnimCurveTransition
        draftShowWindowControls = defaultShowWindowControls
        draftWindowButtonLayout = defaultWindowButtonLayout
        draftSortBy = defaultSortBy
        draftSortAscending = defaultSortAscending
        draftRememberSortPerFolder = defaultRememberSortPerFolder
        applySettingsNow()
    }

    function syncFromCurrentState() {
        primeOptionSources()
        syncingFromConfig = true
        try {
            draftTheme = config.theme
            draftThemeMode = config.themeMode
            themeOptions = buildOptions(availableThemeValues, draftTheme, "catppuccin-mocha")
            draftLightTheme = config.lightTheme
            draftDarkTheme = config.darkTheme
            lightThemeOptions = buildOptions(availableThemeValues, draftLightTheme, "catppuccin-latte")
            darkThemeOptions = buildOptions(availableThemeValues, draftDarkTheme, "catppuccin-mocha")

            draftFontFamily = config.fontFamily
            fontOptions = buildFontOptions()

            draftIconTheme = config.iconTheme
            iconThemeOptions = buildOptions(availableIconThemeValues, draftIconTheme, "Adwaita")

            draftShowHidden = currentShowHidden
            draftHiddenLast = config.hiddenLast
            draftRightClickToEditPath = config.rightClickToEditPath
            draftSingleClickOpensFolders = config.singleClickOpensFolders
            draftVimNavigation = config.vimNavigation
            draftDependencyStartupCheck = config.dependencyStartupCheck
            draftSidebarVisible = currentSidebarVisible
            draftHiddenQuickAccess = config.hiddenQuickAccess
            draftSidebarPosition = config.sidebarPosition
            draftSplitOrientation = config.splitOrientation
            draftSidebarWidth = currentSidebarWidth
            draftSidebarAutoHideWidth = config.sidebarAutoHideWidth
            draftExpandTransfersByDefault = config.expandTransfersByDefault
            draftRadiusSmall = config.radiusSmall
            draftRadiusMedium = Math.max(config.radiusMedium, draftRadiusSmall)
            draftRadiusLarge = Math.max(config.radiusLarge, draftRadiusMedium)
            draftTransparencyEnabled = config.transparencyEnabled
            draftTransparencyLevel = config.transparencyLevel
            draftAnimationsEnabled = config.animationsEnabled
            draftAnimDurationFast = config.animDurationFast
            draftAnimDuration = config.animDuration
            draftAnimDurationSlow = config.animDurationSlow
            draftAnimCurveEnter = config.animCurveEnter
            draftAnimCurveExit = config.animCurveExit
            draftAnimCurveTransition = config.animCurveTransition
            draftShowWindowControls = config.showWindowControls
            draftWindowButtonLayout = config.windowButtonLayout
            draftSortBy = config.sortBy
            draftSortAscending = config.sortAscending
            draftRememberSortPerFolder = config.rememberSortPerFolder
        } finally {
            syncingFromConfig = false
        }
    }

    function openPanel() {
        syncFromCurrentState()
        showSection(0)
        // Center over the parent window
        if (transientParent) {
            root.x = transientParent.x + Math.round((transientParent.width - root.width) / 2)
            root.y = transientParent.y + Math.round((transientParent.height - root.height) / 2)
        }
        root.show()
        root.raise()
        root.requestActivate()
        root.syncHyprlandRounding()
    }

    function closePanel() {
        flushPendingChanges()
        root.hide()
        root.closed()
    }

    function openRemoteConnect() {
        closePanel()
        remoteConnectRequested()
    }

    function openCloudDriveConnect() {
        closePanel()
        cloudDriveConnectRequested()
    }

    function openKeyboardShortcuts() {
        closePanel()
        keyboardShortcutsRequested()
    }

    function openDependencies() {
        closePanel()
        dependenciesRequested()
    }

    function currentSettings() {
        return {
            theme: draftTheme,
            themeMode: draftThemeMode,
            lightTheme: draftLightTheme,
            darkTheme: draftDarkTheme,
            fontFamily: draftFontFamily,
            iconTheme: draftIconTheme,
            showHidden: draftShowHidden,
            hiddenLast: draftHiddenLast,
            rightClickToEditPath: draftRightClickToEditPath,
            singleClickOpensFolders: draftSingleClickOpensFolders,
            vimNavigation: draftVimNavigation,
            dependencyStartupCheck: draftDependencyStartupCheck,
            sidebarVisible: draftSidebarVisible,
            hiddenQuickAccess: draftHiddenQuickAccess,
            sidebarPosition: draftSidebarPosition,
            splitOrientation: draftSplitOrientation,
            sidebarWidth: draftSidebarWidth,
            sidebarAutoHideWidth: draftSidebarAutoHideWidth,
            expandTransfersByDefault: draftExpandTransfersByDefault,
            radiusSmall: draftRadiusSmall,
            radiusMedium: draftRadiusMedium,
            radiusLarge: draftRadiusLarge,
            transparencyEnabled: draftTransparencyEnabled,
            transparencyLevel: draftTransparencyLevel,
            animationsEnabled: draftAnimationsEnabled,
            animDurationFast: draftAnimDurationFast,
            animDuration: draftAnimDuration,
            animDurationSlow: draftAnimDurationSlow,
            animCurveEnter: draftAnimCurveEnter,
            animCurveExit: draftAnimCurveExit,
            animCurveTransition: draftAnimCurveTransition,
            showWindowControls: draftShowWindowControls,
            windowButtonLayout: draftWindowButtonLayout,
            sortBy: draftSortBy,
            sortAscending: draftSortAscending,
            rememberSortPerFolder: draftRememberSortPerFolder
        }
    }

    function queueSettingsApply() {
        if (syncingFromConfig)
            return

        pendingSettingsDirty = true
        settingsApplyTimer.restart()
    }

    function applyPendingSettings() {
        if (!pendingSettingsDirty)
            return

        pendingSettingsDirty = false
        settingsApplyTimer.stop()
        config.saveSettings(currentSettings())
    }

    function applySettingsNow() {
        if (syncingFromConfig)
            return

        pendingSettingsDirty = true
        applyPendingSettings()
    }

    function flushPendingChanges() {
        applyPendingSettings()
    }

    onClosing: {
        root.flushPendingChanges()
        root.closed()
    }

    Component.onCompleted: {
        root.primeOptionSources()
        root.bindAppearancePreview()
    }

    Timer {
        id: settingsApplyTimer
        interval: 140
        onTriggered: root.applyPendingSettings()
    }

    // Close on Escape
    Shortcut {
        sequence: "Escape"
        enabled: root.visible
        onActivated: root.closePanel()
    }

    Component {
        id: lookPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                spacing: 12

                Text {
                    text: "Appearance"
                    color: Theme.text
                    font.pointSize: Theme.fontNormal + 2
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Q.Dropdown {
                    Layout.preferredWidth: 190
                    label: ""
                    model: ["System", "Light", "Dark", "Custom"]
                    currentIndex: Math.max(0, ["system", "light", "dark", "custom"].indexOf(root.draftThemeMode))
                    onSelected: (index, _) => {
                        root.setDraftThemeMode(["system", "light", "dark", "custom"][index])
                        root.applySettingsNow()
                    }
                }
            }

            Q.Separator { Layout.bottomMargin: 8 }

            Text {
                text: "Theme"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            Q.Dropdown {
                id: themeDropdown
                objectName: "themeDropdown"
                Layout.fillWidth: true
                label: "Theme"
                model: root.themeOptions
                currentIndex: root.optionIndex(root.themeOptions, root.draftTheme, 0)
                onSelected: (_, value) => {
                    root.setDraftTheme(value)
                    root.applySettingsNow()
                }
            }

            Text {
                text: "System follows your desktop appearance preference. Light and Dark use the paired themes below. Choose Custom to select any theme."
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
                Layout.topMargin: 8
                Layout.bottomMargin: 4
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Light theme"
                model: root.lightThemeOptions
                currentIndex: root.optionIndex(root.lightThemeOptions, root.draftLightTheme, 0)
                onSelected: (_, value) => {
                    root.draftLightTheme = value
                    root.applySettingsNow()
                }
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Dark theme"
                model: root.darkThemeOptions
                currentIndex: root.optionIndex(root.darkThemeOptions, root.draftDarkTheme, 0)
                onSelected: (_, value) => {
                    root.draftDarkTheme = value
                    root.applySettingsNow()
                }
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Font"
                model: root.fontOptions
                previewFont: true
                currentIndex: root.optionIndex(root.fontOptions, root.draftFontFamily === "" ? root.systemFontLabel : root.draftFontFamily, 0)
                onSelected: (_, value) => {
                    root.draftFontFamily = value === root.systemFontLabel ? "" : value
                    root.applySettingsNow()
                }
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Icon Pack"
                model: root.iconThemeOptions
                currentIndex: root.optionIndex(root.iconThemeOptions, root.draftIconTheme, 0)
                onSelected: (_, value) => {
                    root.draftIconTheme = value
                    root.applySettingsNow()
                }
            }

            Text {
                text: "Surface Styling"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Transparent containers"
                checked: root.draftTransparencyEnabled
                onToggled: (value) => {
                    root.draftTransparencyEnabled = value
                    root.applySettingsNow()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Transparency"
                from: 0
                to: 100
                stepSize: 1
                showValue: true
                enabled: root.draftTransparencyEnabled
                value: root.draftTransparencyLevel * 100
                onMoved: (value) => {
                    root.draftTransparencyLevel = value / 100
                    root.queueSettingsApply()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Small radius"
                from: 0
                to: 24
                stepSize: 1
                showValue: true
                value: root.draftRadiusSmall
                onMoved: (value) => {
                    root.draftRadiusSmall = Math.round(value)
                    if (root.draftRadiusMedium < root.draftRadiusSmall)
                        root.draftRadiusMedium = root.draftRadiusSmall
                    if (root.draftRadiusLarge < root.draftRadiusMedium)
                        root.draftRadiusLarge = root.draftRadiusMedium
                    root.queueSettingsApply()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Medium radius"
                from: root.draftRadiusSmall
                to: 28
                stepSize: 1
                showValue: true
                value: root.draftRadiusMedium
                onMoved: (value) => {
                    root.draftRadiusMedium = Math.round(value)
                    if (root.draftRadiusLarge < root.draftRadiusMedium)
                        root.draftRadiusLarge = root.draftRadiusMedium
                    root.queueSettingsApply()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Large radius"
                from: root.draftRadiusMedium
                to: 32
                stepSize: 1
                showValue: true
                value: root.draftRadiusLarge
                onMoved: (value) => {
                    root.draftRadiusLarge = Math.round(value)
                    root.queueSettingsApply()
                }
            }
        }
    }

    Component {
        id: layoutPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            Text {
                text: "Browsing"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Show hidden files"
                checked: root.draftShowHidden
                onToggled: (value) => {
                    root.draftShowHidden = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Sort hidden files last"
                checked: root.draftHiddenLast
                onToggled: (value) => {
                    root.draftHiddenLast = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Right click address bar to edit path"
                checked: root.draftRightClickToEditPath
                onToggled: (value) => {
                    root.draftRightClickToEditPath = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Open folders with a single click"
                checked: root.draftSingleClickOpensFolders
                onToggled: (value) => {
                    root.draftSingleClickOpensFolders = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Vim-style file controls"
                checked: root.draftVimNavigation
                onToggled: (value) => {
                    root.draftVimNavigation = value
                    root.applySettingsNow()
                }
            }

            Text {
                Layout.fillWidth: true
                Layout.leftMargin: 12
                text: "Navigate with h/j/k/l, jump with gg and G, select ranges with v, search with /, open with o, trash with dd, copy with y, paste with p, undo with u, redo with Ctrl+r, toggle hidden files with ., focus the sidebar with s, open Settings with i, open shortcuts with Ctrl+k, and toggle Zen mode with z. For example, gg v G y copies everything."
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Split view top and bottom"
                checked: root.draftSplitOrientation === "horizontal"
                onToggled: (value) => {
                    root.draftSplitOrientation = value ? "horizontal" : "vertical"
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Show sidebar"
                checked: root.draftSidebarVisible
                onToggled: (value) => {
                    root.draftSidebarVisible = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Sidebar on right"
                enabled: root.draftSidebarVisible
                checked: root.draftSidebarPosition === "right"
                onToggled: (value) => {
                    root.draftSidebarPosition = value ? "right" : "left"
                    root.applySettingsNow()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Sidebar width"
                from: 160
                to: 480
                stepSize: 10
                showValue: true
                enabled: root.draftSidebarVisible
                value: root.draftSidebarWidth
                onMoved: (value) => {
                    root.draftSidebarWidth = Math.round(value)
                    root.queueSettingsApply()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Hide sidebar in narrow windows"
                checked: root.draftSidebarAutoHideWidth > 0
                onToggled: (value) => {
                    root.draftSidebarAutoHideWidth = value ? root.defaultSidebarAutoHideWidth : 0
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Start file transfers expanded"
                checked: root.draftExpandTransfersByDefault
                onToggled: (value) => {
                    root.draftExpandTransfersByDefault = value
                    root.applySettingsNow()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Hide below window width"
                from: 320
                to: 2000
                stepSize: 20
                showValue: true
                enabled: root.draftSidebarAutoHideWidth > 0
                value: root.draftSidebarAutoHideWidth > 0 ? root.draftSidebarAutoHideWidth
                                                          : root.defaultSidebarAutoHideWidth
                onMoved: (value) => {
                    root.draftSidebarAutoHideWidth = Math.round(value)
                    root.queueSettingsApply()
                }
            }

            Text {
                text: "Icon Size"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Text {
                text: "Ctrl + scroll wheel does the same inside any view."
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                Layout.fillWidth: true
                Layout.bottomMargin: 4
                wrapMode: Text.WordWrap
            }

            // Zoom lives in session.json, not config.toml, so these read and
            // write sessionState directly — no draft value, no
            // applySettingsNow(). Quill's Slider assigns its own `value` while
            // dragging, which breaks the binding, but each section lives in a
            // Loader that is rebuilt every time it is shown, so reopening the
            // page picks up a Ctrl+wheel made in the meantime.
            //
            // The grid zooms by column count, where fewer columns means bigger
            // icons. The slider is flipped so dragging right always enlarges,
            // hence `gridSpan - columns` in both directions.
            Q.Slider {
                objectName: "iconSizeGrid"
                readonly property int gridSpan: 14   // minColumns 2 + maxColumns 12

                Layout.fillWidth: true
                label: "Grid view"
                from: 2
                to: 12
                stepSize: 1
                value: gridSpan - (sessionState.gridColumns > 0 ? sessionState.gridColumns : 7)
                onMoved: (value) => sessionState.gridColumns = gridSpan - Math.round(value)
            }

            Q.Slider {
                objectName: "iconSizeDetailed"
                Layout.fillWidth: true
                label: "Detailed view"
                from: 22
                to: 56
                stepSize: 1
                value: sessionState.rowHeightDetailed > 0 ? sessionState.rowHeightDetailed : 28
                onMoved: (value) => sessionState.rowHeightDetailed = Math.round(value)
            }

            Q.Slider {
                objectName: "iconSizeMiller"
                Layout.fillWidth: true
                label: "Miller view"
                from: 22
                to: 56
                stepSize: 1
                value: sessionState.rowHeightMiller > 0 ? sessionState.rowHeightMiller : 28
                onMoved: (value) => sessionState.rowHeightMiller = Math.round(value)
            }

            Text {
                text: "Quick Access"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Repeater {
                model: root.quickAccessNames

                Q.Toggle {
                    Layout.fillWidth: true
                    label: modelData
                    enabled: root.draftSidebarVisible
                    checked: root.draftHiddenQuickAccess.indexOf(modelData) < 0
                    onToggled: (value) => {
                        root.setQuickAccessVisible(modelData, value)
                        root.applySettingsNow()
                    }
                }
            }

            Text {
                text: "Sorting"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Default sort"
                model: root.sortByLabels
                currentIndex: Math.max(0, root.sortByValues.indexOf(root.draftSortBy))
                onSelected: (index, _) => {
                    root.draftSortBy = root.sortByValues[index]
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Ascending order"
                checked: root.draftSortAscending
                onToggled: (value) => {
                    root.draftSortAscending = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Remember sort per folder"
                checked: root.draftRememberSortPerFolder
                onToggled: (value) => {
                    root.draftRememberSortPerFolder = value
                    root.applySettingsNow()
                }
            }

            Text {
                text: "Window Controls"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Show window controls"
                checked: root.draftShowWindowControls
                onToggled: (value) => {
                    root.draftShowWindowControls = value
                    root.applySettingsNow()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Buttons on left"
                enabled: root.draftShowWindowControls
                checked: root._layoutParts.side === "left"
                onToggled: (value) => {
                    root.rebuildButtonLayout(
                        value ? "left" : "right",
                        root._layoutParts.hasClose,
                        root._layoutParts.hasMinimize,
                        root._layoutParts.hasMaximize
                    )
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Close button"
                enabled: root.draftShowWindowControls
                checked: root._layoutParts.hasClose
                onToggled: (value) => {
                    root.rebuildButtonLayout(root._layoutParts.side, value, root._layoutParts.hasMinimize, root._layoutParts.hasMaximize)
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Minimize button"
                enabled: root.draftShowWindowControls
                checked: root._layoutParts.hasMinimize
                onToggled: (value) => {
                    root.rebuildButtonLayout(root._layoutParts.side, root._layoutParts.hasClose, value, root._layoutParts.hasMaximize)
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                label: "Maximize button"
                enabled: root.draftShowWindowControls
                checked: root._layoutParts.hasMaximize
                onToggled: (value) => {
                    root.rebuildButtonLayout(root._layoutParts.side, root._layoutParts.hasClose, root._layoutParts.hasMinimize, value)
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 54
                radius: Theme.radiusMedium
                color: Theme.containerColor(Theme.surface, 0.22)
                border.width: 1
                border.color: root.sectionBorderColor
                opacity: root.draftShowWindowControls ? 1 : 0.6

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    RowLayout {
                        visible: root._layoutParts.side === "left"
                        spacing: 6

                        Rectangle { visible: root._layoutParts.hasMinimize; width: 12; height: 12; radius: 6; color: Theme.warning }
                        Rectangle { visible: root._layoutParts.hasMaximize; width: 12; height: 12; radius: 6; color: Theme.success }
                        Rectangle { visible: root._layoutParts.hasClose; width: 12; height: 12; radius: 6; color: Theme.error }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 6
                        radius: 3
                        color: Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.08)
                    }

                    RowLayout {
                        visible: root._layoutParts.side !== "left"
                        spacing: 6

                        Rectangle { visible: root._layoutParts.hasMinimize; width: 12; height: 12; radius: 6; color: Theme.warning }
                        Rectangle { visible: root._layoutParts.hasMaximize; width: 12; height: 12; radius: 6; color: Theme.success }
                        Rectangle { visible: root._layoutParts.hasClose; width: 12; height: 12; radius: 6; color: Theme.error }
                    }
                }
            }
        }
    }

    Component {
        id: motionPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                Layout.bottomMargin: 8
                spacing: 12

                Text {
                    text: "Animations"
                    color: Theme.text
                    font.pointSize: Theme.fontNormal + 2
                    font.bold: true
                }

                Item { Layout.fillWidth: true }

                Q.Toggle {
                    label: ""
                    checked: root.draftAnimationsEnabled
                    onToggled: (value) => {
                        root.draftAnimationsEnabled = value
                        root.applySettingsNow()
                    }
                }
            }

            Q.Separator { Layout.bottomMargin: 8 }

            Text {
                text: "Timing"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Fast"
                from: 0
                to: 500
                stepSize: 10
                showValue: true
                enabled: root.draftAnimationsEnabled
                value: root.draftAnimDurationFast
                onMoved: (value) => {
                    root.draftAnimDurationFast = Math.round(value)
                    root.queueSettingsApply()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Normal"
                from: 0
                to: 1000
                stepSize: 10
                showValue: true
                enabled: root.draftAnimationsEnabled
                value: root.draftAnimDuration
                onMoved: (value) => {
                    root.draftAnimDuration = Math.round(value)
                    root.queueSettingsApply()
                }
            }

            Q.Slider {
                Layout.fillWidth: true
                label: "Slow"
                from: 0
                to: 1500
                stepSize: 10
                showValue: true
                enabled: root.draftAnimationsEnabled
                value: root.draftAnimDurationSlow
                onMoved: (value) => {
                    root.draftAnimDurationSlow = Math.round(value)
                    root.queueSettingsApply()
                }
            }

            Text {
                text: "Curves"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.topMargin: 12
                Layout.bottomMargin: 4
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Enter"
                enabled: root.draftAnimationsEnabled
                model: root.curveOptions
                currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveEnter))
                onSelected: (_, value) => {
                    root.draftAnimCurveEnter = value
                    root.applySettingsNow()
                }
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Exit"
                enabled: root.draftAnimationsEnabled
                model: root.curveOptions
                currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveExit))
                onSelected: (_, value) => {
                    root.draftAnimCurveExit = value
                    root.applySettingsNow()
                }
            }

            Q.Dropdown {
                Layout.fillWidth: true
                label: "Transition"
                enabled: root.draftAnimationsEnabled
                model: root.curveOptions
                currentIndex: Math.max(0, root.curveOptions.indexOf(root.draftAnimCurveTransition))
                onSelected: (_, value) => {
                    root.draftAnimCurveTransition = value
                    root.applySettingsNow()
                }
            }
        }
    }

    Component {
        id: drivePageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: "Connect cloud storage to browse and transfer files from your cloud accounts."
                color: Theme.subtext
                font.pointSize: Theme.fontNormal
                wrapMode: Text.WordWrap
            }

            Q.Button {
                Layout.alignment: Qt.AlignLeft
                text: "Add a cloud drive"
                onClicked: root.openCloudDriveConnect()
            }

            Text {
                Layout.topMargin: 8
                text: "Connected drives"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
            }

            Text {
                visible: rcloneService.remoteNames.length === 0
                Layout.fillWidth: true
                text: "No cloud drives are configured yet."
                color: Theme.muted
                font.pointSize: Theme.fontSmall
            }

            Repeater {
                model: rcloneService.remoteNames

                delegate: Rectangle {
                    required property string modelData
                    Layout.fillWidth: true
                    implicitHeight: 38
                    readonly property var activeMountsSnapshot: rcloneService.activeMounts
                    readonly property bool mounted: activeMountsSnapshot.indexOf(modelData) >= 0
                        && rcloneService.isMounted(modelData)
                    radius: Math.min(Theme.radiusSmall, 4)
                    color: Theme.containerColor(Theme.surface, 0.45)
                    border.width: 1
                    border.color: root.sectionBorderColor

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        IconCloud {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            color: parent.parent.mounted ? Theme.accent : Theme.subtext
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            color: Theme.text
                            font.pointSize: Theme.fontNormal
                            elide: Text.ElideRight
                        }

                        Text {
                            text: parent.parent.mounted ? "Mounted" : "Not mounted"
                            color: parent.parent.mounted ? Theme.success : Theme.muted
                            font.pointSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }

    Component {
        id: toolsPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 6

            Text {
                text: "Utilities"
                color: Theme.accent
                font.pointSize: Theme.fontSmall
                font.bold: true
                Layout.bottomMargin: 4
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Q.Button {
                    Layout.fillWidth: true
                    text: "Keyboard Shortcuts"
                    onClicked: root.openKeyboardShortcuts()
                }

                Q.Button {
                    Layout.fillWidth: true
                    text: "Connect to Network Location"
                    variant: "ghost"
                    onClicked: root.openRemoteConnect()
                }

                Q.Button {
                    Layout.fillWidth: true
                    text: "Check Dependencies"
                    variant: "ghost"
                    onClicked: root.openDependencies()
                }
            }

            Q.Toggle {
                Layout.fillWidth: true
                Layout.topMargin: 8
                label: "Warn about missing dependencies on startup"
                checked: root.draftDependencyStartupCheck
                onToggled: (value) => {
                    root.draftDependencyStartupCheck = value
                    root.applySettingsNow()
                }
            }
        }
    }

    Component {
        id: pluginsPageComponent

        ColumnLayout {
            width: pageLoader.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Q.TextField {
                    id: pluginRepositoryUrl
                    Layout.fillWidth: true
                    placeholder: "HTTPS or SSH Git repository URL"
                }

                Q.Button {
                    text: pluginManager.installingRepository ? "Installing…" : "Install"
                    enabled: !pluginManager.installingRepository
                    onClicked: {
                        if (pluginManager.installFromRepository(pluginRepositoryUrl.text))
                            pluginRepositoryUrl.text = ""
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                text: pluginManager.repositoryInstallStatus
                visible: text !== ""
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Install Lua plugins from Git repositories or in “%1”. Review their requested permissions before enabling them; plugins run with the same permissions as Aether."
                        .arg(pluginManager.pluginDirectory)
                    color: Theme.subtext
                    font.pointSize: Theme.fontSmall
                    wrapMode: Text.WordWrap
                }
                Q.Button {
                    text: "Refresh"
                    onClicked: pluginManager.refresh()
                }
            }

            Repeater {
                model: pluginManager
                delegate: Rectangle {
                    required property string pluginId
                    required property string name
                    required property string version
                    required property string description
                    required property string author
                    required property bool enabled
                    required property string error
                    required property var actions
                    required property var settings
                    required property var permissions
                    Layout.fillWidth: true
                    implicitHeight: content.implicitHeight + 24
                    radius: Theme.radiusMedium
                    color: Theme.containerColor(Theme.surface, 0.22)
                    border.width: 1
                    border.color: root.sectionBorderColor
                    ColumnLayout {
                        id: content
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Text { text: name; color: Theme.text; font.bold: true; font.pointSize: Theme.fontNormal }
                                Text { text: [pluginId, version, author].filter(Boolean).join(" · "); color: Theme.subtext; font.pointSize: Theme.fontSmall }
                            }
                            Q.Toggle {
                                checked: enabled
                                enabled: !error
                                label: "Enabled"
                                onToggled: (value) => pluginManager.setEnabled(pluginId, value)
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: description.length > 0 || error.length > 0
                            text: error.length ? error : description
                            color: error.length ? Theme.error : Theme.subtext
                            font.pointSize: Theme.fontSmall
                            wrapMode: Text.WordWrap
                        }
                        Repeater {
                            model: permissions
                            delegate: Q.Toggle {
                                required property var modelData
                                label: "Allow: " + modelData.name
                                checked: modelData.enabled
                                onToggled: (value) => pluginManager.setPermission(pluginId, modelData.name, value)
                            }
                        }
                        Repeater {
                            model: enabled && !error ? actions : []
                            delegate: Q.Button {
                                required property int index
                                required property string modelData
                                text: modelData
                                onClicked: pluginManager.triggerAction(pluginId, index)
                            }
                        }
                        Repeater {
                            model: settings
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: sectionContent.implicitHeight + 20
                                radius: Theme.radiusMedium
                                color: Theme.containerColor(Theme.surface, 0.14)
                                border.width: 1
                                border.color: root.sectionBorderColor
                                ColumnLayout {
                                    id: sectionContent
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 8
                                    Text { text: modelData.title; color: Theme.text; font.bold: true; font.pointSize: Theme.fontNormal }
                                    Text {
                                        Layout.fillWidth: true
                                        visible: modelData.description.length > 0
                                        text: modelData.description
                                        color: Theme.subtext
                                        font.pointSize: Theme.fontSmall
                                        wrapMode: Text.WordWrap
                                    }
                                    Repeater {
                                        model: modelData.controls
                                        delegate: Loader {
                                            required property var modelData
                                            Layout.fillWidth: true
                                            property string ownerPluginId: pluginId
                                            property var controlData: modelData
                                            sourceComponent: controlData.type === "toggle" ? settingToggle
                                                : controlData.type === "choice" ? settingChoice
                                                : controlData.type === "number" ? settingNumber : settingText
                                            function syncControl() {
                                                if (item) {
                                                    item.ownerPluginId = ownerPluginId
                                                    item.controlData = controlData
                                                }
                                            }
                                            onLoaded: syncControl()
                                            onControlDataChanged: syncControl()
                                            onOwnerPluginIdChanged: syncControl()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Component {
                id: settingToggle
                Q.Toggle {
                    property string ownerPluginId: ""
                    property var controlData: ({})
                    label: controlData.label || controlData.key
                    checked: controlData.value === true
                    onToggled: (value) => pluginManager.setSetting(ownerPluginId, controlData.key, value)
                }
            }
            Component {
                id: settingText
                ColumnLayout {
                    property string ownerPluginId: ""
                    property var controlData: ({})
                    spacing: 4
                    Text { text: controlData.label || controlData.key; color: Theme.text; font.pointSize: Theme.fontSmall }
                    Q.TextField {
                        text: String(controlData.value ?? "")
                        placeholder: controlData.placeholder || ""
                        onTextEdited: (value) => pluginManager.setSetting(parent.ownerPluginId, parent.controlData.key, value)
                    }
                }
            }
            Component {
                id: settingNumber
                ColumnLayout {
                    property string ownerPluginId: ""
                    property var controlData: ({})
                    spacing: 4
                    Text { text: controlData.label || controlData.key; color: Theme.text; font.pointSize: Theme.fontSmall }
                    Q.TextField {
                        text: String(controlData.value ?? "")
                        placeholder: controlData.placeholder || "Number"
                        onTextEdited: (value) => {
                            const numberValue = Number(value)
                            if (value !== "" && Number.isFinite(numberValue))
                                pluginManager.setSetting(parent.ownerPluginId, parent.controlData.key, numberValue)
                        }
                    }
                }
            }
            Component {
                id: settingChoice
                Q.Dropdown {
                    property string ownerPluginId: ""
                    property var controlData: ({})
                    label: controlData.label || controlData.key
                    model: controlData.options || []
                    currentIndex: Math.max(0, model.indexOf(String(controlData.value ?? "")))
                    onSelected: (index, value) => pluginManager.setSetting(ownerPluginId, controlData.key, value)
                }
            }

            Text {
                Layout.fillWidth: true
                visible: pluginManager.count === 0
                text: "No plugins installed yet. Each plugin belongs in its own folder and needs a plugin.json manifest and main.lua entry file."
                color: Theme.subtext
                font.pointSize: Theme.fontSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    Item {
        id: pageContainer
        anchors.fill: parent

        // One height for every section. Hyprland keeps a floating window at
        // the size it mapped with, so shrinking for a shorter section left a
        // stale band of the previous page below the content (issue #12).
        // Sections taller than this scroll inside contentFlick.
        implicitHeight: Math.max(460, Math.min(640,
            (root.transientParent ? root.transientParent.height : 768) - 140))

        Rectangle {
            anchors.fill: parent
            color: Theme.containerColor(Theme.mantle, 0.9)
            border.width: 1
            border.color: root.sectionBorderColor

            Rectangle {
                id: closeButton
                z: 10
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.rightMargin: 8
                width: 28
                height: 28
                radius: Theme.radiusSmall
                color: closeHover.hovered
                    ? Qt.rgba(Theme.text.r, Theme.text.g, Theme.text.b, 0.1)
                    : "transparent"

                IconX {
                    anchors.centerIn: parent
                    size: 16
                    color: Theme.text
                }

                HoverHandler { id: closeHover }
                TapHandler { onTapped: root.closePanel() }
            }

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    Rectangle {
                        visible: !root.compactNavigation
                        Layout.fillHeight: true
                        Layout.preferredWidth: 184
                        color: Theme.containerColor(Theme.crust, 0.96)

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            anchors.topMargin: 16
                            spacing: 2

                            Row {
                                Layout.leftMargin: 12
                                Layout.bottomMargin: 12

                                Text {
                                    text: "Settings"
                                    color: Theme.text
                                    font.pointSize: Theme.fontNormal + 1
                                    font.bold: true
                                }
                            }

                            Q.Tabs {
                                id: sideTabs
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                orientation: Qt.Vertical
                                model: root.sectionNavItems
                                labelRole: "title"
                                iconComponentRole: "iconComponent"
                                currentIndex: root.currentSectionIndex
                                sideTabHeight: 36
                                sideTabWidth: 168
                                onTabChanged: (index) => root.showSection(index)
                            }

                            Q.Button {
                                Layout.fillWidth: true
                                Layout.leftMargin: 12
                                Layout.rightMargin: 12
                                Layout.topMargin: 8
                                text: "Reset to Defaults"
                                variant: "ghost"
                                onClicked: root.resetToDefaults()
                            }
                        }
                    }

                    Q.Separator {
                        visible: !root.compactNavigation
                        orientation: Qt.Vertical
                        Layout.fillHeight: true
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 20
                            spacing: 12

                            Q.Tabs {
                                id: compactSectionNav
                                visible: root.compactNavigation
                                Layout.fillWidth: true
                                model: root.sectionNavItems
                                labelRole: "title"
                                currentIndex: root.currentSectionIndex
                                onTabChanged: (index) => root.showSection(index)
                            }

                            Text {
                                text: root.sectionItems[root.currentSectionIndex].title
                                color: Theme.text
                                font.pointSize: Theme.fontLarge + 2
                                font.bold: true
                            }

                            Flickable {
                                id: contentFlick
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                clip: true
                                contentWidth: width
                                contentHeight: pageLoader.item ? pageLoader.item.implicitHeight : 0
                                boundsBehavior: Flickable.StopAtBounds
                                interactive: contentHeight > height

                                Loader {
                                    id: pageLoader
                                    width: contentFlick.width
                                    sourceComponent: root.currentSectionIndex === 0
                                        ? lookPageComponent
                                        : root.currentSectionIndex === 1
                                            ? layoutPageComponent
                                            : root.currentSectionIndex === 2
                                                ? motionPageComponent
                                                : root.currentSectionIndex === 3
                                                    ? drivePageComponent
                                                    : root.currentSectionIndex === 4
                                                        ? toolsPageComponent
                                                        : pluginsPageComponent
                                }
                            }
                        }
                    }
                }

            }
        }
    }
}
