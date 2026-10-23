-- This file runs when the plugin is enabled.
aether.log("Hello panel loaded")

-- Add an action button to this plugin's card in Settings → Plugins.
aether.addAction("Say hello", function()
    aether.log("Hello button clicked")
end)

aether.addToolbarButton({ label = "Hello", icon = "Sparkles", run = function()
    aether.log("Toolbar action clicked")
end })

aether.addShortcut({ name = "Say hello", shortcut = "Ctrl+Alt+H", run = function()
    aether.log("Keyboard shortcut activated")
end })

aether.on("folder.opened", function(context)
    aether.log("Opened folder: " .. context.path)
end)
