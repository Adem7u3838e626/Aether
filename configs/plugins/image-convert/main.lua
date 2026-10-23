-- Adds a right-click submenu to image files. Requires ImageMagick's `magick`
-- command to be available on PATH.
aether.addContextMenu({
    label = "Convert into",
    mimeTypes = { "image/*" },
    actions = {
        {
            label = "PNG",
            run = function(context)
                for _, path in ipairs(context.paths) do
                    local base = path:gsub("%.[^%.]+$", "")
                    local output = base .. ".converted.png"
                    if not aether.run("magick", { path, output }) then
                        aether.log("Could not start ImageMagick for " .. path)
                    end
                end
            end
        },
        {
            label = "JPEG",
            run = function(context)
                for _, path in ipairs(context.paths) do
                    local base = path:gsub("%.[^%.]+$", "")
                    local output = base .. ".converted.jpg"
                    if not aether.run("magick", { path, output }) then
                        aether.log("Could not start ImageMagick for " .. path)
                    end
                end
            end
        },
        {
            label = "WebP",
            run = function(context)
                for _, path in ipairs(context.paths) do
                    local base = path:gsub("%.[^%.]+$", "")
                    local output = base .. ".converted.webp"
                    if not aether.run("magick", { path, output }) then
                        aether.log("Could not start ImageMagick for " .. path)
                    end
                end
            end
        }
    }
})
