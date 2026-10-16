-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- "Exit OpenLF2?" confirmation (OpenLF2 extension), opened by Escape or Android's back button over any screen
-- that does not use Escape itself. It looks like the launch screen's options window and is driven the same way:
-- arrows/d-pad move the focus, Enter/A confirm, Escape/B (or the back button again) stay; a mouse click or a tap
-- presses a button.
local navigation = require("base/ui/navigation")
local font = require("base/ui/font")
local sounds = require("base/game/sounds")

local dialog = {}

local panel = {x = 247, y = 215, width = 300, height = 120}
local exit_button = {x = panel.x + 24, y = panel.y + 72, width = 120, height = 26, label = "Exit"}
local stay_button = {x = panel.x + 156, y = panel.y + 72, width = 120, height = 26, label = "Cancel"}
local title = "Exit OpenLF2?"

local function area_target(area)
    return navigation.target(area.x, area.y, area.x + area.width, area.y + area.height)
end
local targets = {area_target(exit_button), area_target(stay_button)}

local function inside(x, y, area)
    return x >= area.x and x <= area.x + area.width and y >= area.y and y <= area.y + area.height
end

function dialog.create()
    -- Focus starts on Cancel. Nothing counts until the keys and buttons that opened the dialog are let go.
    return {nav = navigation.create(), ready = false, pointer_x = 0, pointer_y = 0}
end

-- Returns "exit" or "close" when the player has decided.
function dialog.update(state, input, context)
    local keys, pad = input.keys, input.gamepad or {}
    local pressed = false
    for _, code in ipairs(input.presses or {}) do
        if code == 0x1b then pressed = true end
    end
    if not state.ready then
        if not (keys[0x1b] or keys[0x0d] or pad.a or pad.b or pad.s or pad.k) then state.ready = true end
        state.pointer_x, state.pointer_y = input.pointer_x, input.pointer_y
        return nil
    end
    local view = navigation.apply(state.nav, input, {page = "quit", targets = targets, default = 2, autofocus = true})
    state.pointer_x, state.pointer_y = view.pointer_x, view.pointer_y
    local result
    if pressed or view.nav.cancel then
        result = "close"
    elseif view.click then
        if inside(view.pointer_x, view.pointer_y, exit_button) then result = "exit"
        elseif inside(view.pointer_x, view.pointer_y, stay_button) then result = "close" end
    end
    if result then sounds.direct(state, result == "exit" and "ok" or "cancel") end
    for _, sound in ipairs(sounds.flush(state)) do context.sound(sound.resource, sound.volume, sound.pan) end
    return result
end

local function bold(context, text, x, y, variant)
    for _, offset in ipairs({{-1, 1}, {-1, 0}, {0, 1}, {0, 0}}) do
        font.draw(context, text, x + offset[1], y + offset[2], variant)
    end
end

local function button(context, state, area)
    local pointed = state.ready and inside(state.pointer_x, state.pointer_y, area)
    context.fill(area.x, area.y, area.width, area.height, 0xa0, 0xa0, 0xc0)
    context.fill(area.x + 1, area.y + 1, area.width - 2, area.height - 2,
        pointed and 0x32 or 0x10, pointed and 0x4d or 0x20, pointed and 0x9a or 0x50)
    bold(context, area.label, area.x + math.floor((area.width - #area.label * 8) / 2), area.y + 5, pointed and 1 or 0)
end

function dialog.draw(state, context)
    context.fill(panel.x, panel.y, panel.width, panel.height, 0xa0, 0xa0, 0xc0)
    context.fill(panel.x + 2, panel.y + 2, panel.width - 4, panel.height - 4, 0x10, 0x20, 0x50)
    bold(context, title, panel.x + math.floor((panel.width - #title * 8) / 2), panel.y + 24, 1)
    button(context, state, exit_button)
    button(context, state, stay_button)
    -- The pointer, for the mouse (a keyboard or controller focus parks it on a button).
    if state.nav.mode == "pointer" then
        context.image("pe/lf2_cursor", math.min(state.pointer_x, 0x307), math.min(state.pointer_y + 2, 0x217), true)
    end
end

return dialog
