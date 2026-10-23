-- Demonstrates type-filtered context menus and argument-safe external tools.
-- Install ImageMagick (`magick`) and/or FFmpeg (`ffmpeg`) for the formats used.

local function outputPath(input, extension)
    local base = input:gsub("%.[^%.]+$", "")
    return base .. ".converted." .. extension
end

local function runForFiles(context, program, makeArguments)
    for _, input in ipairs(context.paths) do
        local arguments = makeArguments(input, outputPath)
        if not aether.run(program, arguments) then
            aether.log(program .. " was not found; could not convert " .. input)
        end
    end
end

aether.addContextMenu({
    label = "Convert image into",
    icon = "Image",
    mimeTypes = { "image/*" },
    actions = {
        { label = "PNG", icon = "Image", run = function(context)
            runForFiles(context, "magick", function(input, output)
                return { input, output(input, "png") }
            end)
        end },
        { label = "JPEG", icon = "Image", run = function(context)
            runForFiles(context, "magick", function(input, output)
                return { input, output(input, "jpg") }
            end)
        end },
        { label = "WebP", icon = "Image", run = function(context)
            runForFiles(context, "magick", function(input, output)
                return { input, output(input, "webp") }
            end)
        end }
    }
})

aether.addContextMenu({
    label = "Convert audio into",
    icon = "Music",
    mimeTypes = { "audio/*" },
    actions = {
        { label = "MP3", icon = "Music", run = function(context)
            runForFiles(context, "ffmpeg", function(input, output)
                return { "-n", "-i", input, output(input, "mp3") }
            end)
        end },
        { label = "WAV", icon = "Music", run = function(context)
            runForFiles(context, "ffmpeg", function(input, output)
                return { "-n", "-i", input, output(input, "wav") }
            end)
        end }
    }
})

aether.addContextMenu({
    label = "Convert video into",
    icon = "Video",
    mimeTypes = { "video/*" },
    actions = {
        { label = "MP4 (H.264)", icon = "Video", run = function(context)
            runForFiles(context, "ffmpeg", function(input, output)
                return { "-n", "-i", input, "-c:v", "libx264", "-c:a", "aac", output(input, "mp4") }
            end)
        end },
        { label = "WebM (VP9)", icon = "Video", run = function(context)
            runForFiles(context, "ffmpeg", function(input, output)
                return { "-n", "-i", input, "-c:v", "libvpx-vp9", "-c:a", "libopus", output(input, "webm") }
            end)
        end }
    }
})
