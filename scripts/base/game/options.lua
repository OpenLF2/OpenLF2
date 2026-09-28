-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- OpenLF2's own settings, which the original game does not have. They
-- live in config.json's "options" member, next to the original's controls and recording values.
local unlock = require("base/game/unlock")
local options = {}

-- Every valid value, in the dropdown's order. "xbrz" is a shader: the host offers it only on
-- renderers that can run it (see options.available_filters), but a saved choice stays valid.
options.upscaling_filters = {"nearest", "linear", "xbrz"}
-- The values to offer: `features` is what the host reported ({xbrz = true} when it can run the shader).
function options.available_filters(features)
    local list = {}
    for _, filter in ipairs(options.upscaling_filters) do
        if filter ~= "xbrz" or (features and features.xbrz) then list[#list + 1] = filter end
    end
    return list
end
local defaults = {unlock_characters = false, upscaling_filter = "nearest", fullscreen = false, show_fps = false,
    rumble = false}
local current

local function valid_filter(value)
    for _, filter in ipairs(options.upscaling_filters) do
        if value == filter then return true end
    end
    return false
end
local function copy(values)
    return {unlock_characters = values.unlock_characters,
        upscaling_filter = valid_filter(values.upscaling_filter) and values.upscaling_filter or defaults.upscaling_filter,
        fullscreen = values.fullscreen, show_fps = values.show_fps, rumble = values.rumble}
end

-- The saved options, or the defaults for anything missing or invalid.
function options.load()
    local document = engine.read_settings()
    local saved = type(document) == "table" and document.options
    local values = copy(defaults)
    if type(saved) == "table" and type(saved.unlock_characters) == "boolean" then
        values.unlock_characters = saved.unlock_characters
    end
    if type(saved) == "table" and valid_filter(saved.upscaling_filter) then
        values.upscaling_filter = saved.upscaling_filter
    end
    if type(saved) == "table" and type(saved.fullscreen) == "boolean" then
        values.fullscreen = saved.fullscreen
    end
    if type(saved) == "table" and type(saved.show_fps) == "boolean" then
        values.show_fps = saved.show_fps
    end
    if type(saved) == "table" and type(saved.rumble) == "boolean" then
        values.rumble = saved.rumble
    end
    current = values
    return copy(values)
end
function options.current()
    if not current then options.load() end
    return copy(current)
end

local function apply(before, after)
    if after.unlock_characters and not (before and before.unlock_characters) then
        unlock.flags.characters = 1
    elseif before and before.unlock_characters and not after.unlock_characters then
        unlock.flags.characters = 0
    end
end

-- At startup: load and apply the saved options.
function options.start()
    apply(nil, options.load())
end

-- Saves `values` into config.json (other members are kept) and applies what changed. Returns
-- true, or nil and a message; the options apply even when saving fails.
function options.save(values)
    local before = options.current()
    current = copy(values)
    apply(before, current)
    local document = engine.read_settings()
    if type(document) ~= "table" then document = {} end
    document.version = 1
    document.options = copy(current)
    return engine.write_settings(document)
end
return options
