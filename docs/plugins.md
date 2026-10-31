# Aether Lua plugins

Aether discovers plugins in the directory shown at Settings → Plugins. Each
plugin has its own folder containing a `plugin.json` manifest and Lua entry
file:

```text
~/.config/aether/plugins/
└── hello-panel/
    ├── plugin.json
    └── main.lua
```

The manifest looks like this:

```json
{
  "id": "hello-panel",
  "name": "Hello panel",
  "version": "1.0.0",
  "description": "A Lua plugin example.",
  "author": "Plugin author",
  "apiVersion": 1,
  "permissions": ["process", "filesystem.read"],
  "entry": "main.lua"
}
```

The `id` must match the folder name. `entry` defaults to `main.lua` and must
resolve to a file inside that folder. The repository has a ready-to-copy sample
in `configs/plugins/hello-panel`. Copy that folder to the plugin directory,
press **Refresh** in Settings → Plugins, and enable it. Lua plugins need no
separate compilation step.

When enabled, Aether executes the Lua entry file and retains its Lua state
while the plugin is enabled. The currently supported API is:

```lua
aether.log("Plugin started")

aether.addAction("Say hello", function()
    aether.log("Button clicked")
end)
```

`aether.addAction(label, callback)` adds a button to the plugin card. The
callback runs when the user clicks it. Disabling the plugin closes its Lua
state and removes its actions. Enabled state is stored in
`plugins-enabled.json` under the user's config directory.

Plugins can add right-click menu submenus for matching files:

```lua
aether.addContextMenu({
    label = "Convert into",
    icon = "Image",
    contexts = { "file" }, -- defaults to file for older plugins
    mimeTypes = { "image/*" },
    extensions = { "png", "jpg" }, -- either filter can be used
    actions = {
        { label = "PNG", icon = "Image", run = function(context)
            aether.log("Convert " .. context.path .. " to PNG")
        end }
    }
})
```

The menu appears only when the target file (and every selected file) matches a
declared MIME type or extension. The callback receives target details such as
`context.path`, `context.fileName`, `context.baseName`, `context.extension`,
`context.parentPath`, `context.isDirectory`, `context.mimeType`, and the readable
`context.fileType`. `context.paths` lists all selected paths; `context.files`,
`context.folders`, and `context.selected` split them into file paths, folder
paths, and full metadata records. Plugins can also call `aether.fileInfo(path)`
to inspect a path or `aether.listDirectory(path)` to list the entries in a
folder. Each metadata record includes its name, path, parent, extension, MIME
type, readable type, size, and whether it is a file or directory. These helpers
return Lua tables that plugin code can use directly:

```lua
local info = aether.fileInfo(context.path)
aether.log(info.fileName .. " is a " .. info.fileType)

for _, entry in ipairs(aether.listDirectory(context.parentPath)) do
    if entry.isDirectory then
        aether.log("Folder: " .. entry.fileName)
    end
end
```

Plugins can also use Aether's file operation service:

```lua
aether.open(path)
aether.rename(path, newName)             -- rename in the same folder
local copyId = aether.copy(paths, dest)  -- returns transfer ID, or -1
local moveId = aether.move(paths, dest)  -- returns transfer ID, or -1
local trashId = aether.trash(paths)      -- moves items to system trash
```

`copy`, `move`, and `trash` accept one path or a Lua array of paths. Transfer
operations appear in Aether's normal progress UI.

Plugins can add toolbar buttons and keyboard shortcuts:

```lua
aether.addToolbarButton({ label = "Compress", icon = "FolderArchive", run = function()
    aether.log("Toolbar button clicked")
end })

aether.addShortcut({ name = "Quick compress", shortcut = "Ctrl+Alt+C", run = function()
    aether.log("Shortcut activated")
end })
```

Both callbacks can use the same Aether APIs as context-menu actions. A shortcut
uses Qt key sequence notation; avoid binding a shortcut already used by Aether
or another plugin.

Plugins can listen for lifecycle events:

```lua
aether.on("app.started", function(context) end)
aether.on("folder.opened", function(context)
    aether.log("Now viewing " .. context.path .. " in " .. context.pane)
end)
aether.on("files.selected", function(context)
    -- context.paths is the current selection; context.pane identifies the pane.
end)
```

`aether.on` currently supports `app.started`, `folder.opened`, and
`files.selected`. Each callback receives an event-specific Lua table.

The manifest `apiVersion` currently supports version `1`; unsupported values
keep the plugin visible in Settings but prevent it from running. Use the
manifest `permissions` list to declare access such as `process`,
`filesystem.read`, and `filesystem.write`. Users can grant or revoke each
declared permission from the plugin card. Declared permissions start disabled.
The file and process APIs return failure values when permission is missing.
Plugins without a `permissions` field keep legacy API behavior.

Plugins can add one or more configurable sections to their Settings card. The
supported controls are `toggle`, `text`, `number`, and `choice`; values persist
in `plugin-settings.json` and are available to Lua through `getSetting` and
`setSetting`:

```lua
aether.addSettingsSection({
    title = "Converter",
    description = "Choose the output format and quality.",
    controls = {
        { type = "choice", key = "format", label = "Format",
          options = { "png", "jpg", "webp" }, default = "png" },
        { type = "number", key = "quality", label = "Quality", default = 85 },
        { type = "toggle", key = "keepOriginal", label = "Keep original", default = true },
        { type = "text", key = "outputFolder", label = "Output folder",
          placeholder = "Same folder by default", default = "" }
    }
})

local format = aether.getSetting("format", "png")
aether.setSetting("lastUsedFormat", format)
```

Every control requires a unique `key` within its plugin and a `label` is
recommended. `choice` also requires an `options` array. Sections are declared
when the enabled plugin starts, and their saved values remain available after
restarting Aether.

The `configs/plugins/external-converter` example adds image, audio, and video
conversion submenus. It uses `aether.run(program, args)` to invoke ImageMagick
and FFmpeg without a shell. Install those tools separately for the formats you
want to convert.

Both `addContextMenu` and each action accept an optional `icon` name, such as
`"Image"`, `"Music"`, or `"Video"`. These names refer to built-in icon
components named `Icon<Name>.qml` under `src/qml/icons`.

`contexts` selects where a menu appears: `file`, `directory`, `empty`, or
`sidebar`. File menus still require `extensions` or `mimeTypes`; the other
contexts do not. A callback receives `context.contextType` and a `context`
table. Empty-space properties include `currentPath`, `pane`, `viewMode`, and
`isTrashView`. Sidebar properties include the clicked entry's fields, such as
`kind`, `name`, `path`, `mounted`, `remoteName`, `primaryPath`, and
`secondaryPath`, depending on the entry type.

For example, a plugin can target directories, empty space, and sidebar entries:

```lua
aether.addContextMenu({
    label = "Folder tools",
    contexts = { "directory", "empty", "sidebar" },
    actions = {
        { label = "Show target", run = function(context)
            aether.log(context.contextType .. ": " .. (context.path or ""))
            aether.log("Entry kind: " .. ((context.context and context.context.kind) or "view"))
        end }
    }
})
```

Plugin scripts run inside Aether with the user's permissions and are not a
security sandbox. Install code only from authors you trust.
