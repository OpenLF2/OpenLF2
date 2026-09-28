-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- OpenLF2 extension: on-screen touch gamepad for player 1 during a live match. Shows only
-- when the last input was touch-driven; other pages already handle direct taps.
--
-- C++ only reports held fingers in the fixed 794x550 viewport; this module owns all
-- hit-testing. Fixed layout coordinates already adapt to any screen size since the platform
-- scales the viewport.
local font = require("base/ui/font")
local gamepad = {}

-- Left floating joystick: a finger down in the zone recenters the base; direction comes from
-- the knob's offset past a dead zone (8-way, diagonals set two). Keeps following that finger
-- outside the zone.
-- `run_distance`: running needs a double-tap, not a hold; past this distance the stick fakes
-- that double-tap itself (run_pulse), since a held stick never releases.
local stick = {zone = {left = 0, top = 290, right = 330, bottom = 520},
    default_x = 125, default_y = 440, radius = 56, travel = 60, deadzone = 14, threshold = 0.4,
    run_distance = 51, run_rearm_distance = 30}
-- Right action buttons: attack is primary/biggest (thumb rests there); jump/defend are reached
-- by rolling the thumb.
local buttons = {
    {action = "c", label = "ATK", x = 696, y = 458, radius = 44,
        fill = {90, 45, 40}, pressed = {170, 80, 65}},
    {action = "b", label = "JUMP", x = 704, y = 364, radius = 32,
        fill = {40, 80, 50}, pressed = {80, 160, 100}},
    {action = "f", label = "DEF", x = 602, y = 400, radius = 32,
        fill = {40, 55, 90}, pressed = {75, 105, 175}},
}
local outline = {18, 18, 22}

-- The run pulse: release/press/release/press, mimicking a human double-tap within the dash
-- window. Each step holds two frames since input is only re-read every other frame.
local run_pulse_steps = {false, false, true, true, false, false, true, true}

function gamepad.create()
    return {finger = nil, base_x = stick.default_x, base_y = stick.default_y,
        knob_x = stick.default_x, knob_y = stick.default_y, held = {}, visible = false,
        run_armed = nil, run_pulse = nil, run_pulse_step = 0}
end

local function clamp(value, low, high) return math.max(low, math.min(high, value)) end
local function inside_zone(zone, x, y) return x >= zone.left and x <= zone.right and y >= zone.top and y <= zone.bottom end
local function inside_circle(cx, cy, radius, x, y) return (x - cx) ^ 2 + (y - cy) ^ 2 <= radius * radius end

-- state: from `create`. input: the frame's input; pass touch_active=false to release the held
-- finger and hide (paused/replaying/other screens).
-- Returns held letters ("udlrcbf" subset), or "" while hidden; a run pulse briefly overrides
-- l/r on its own.
function gamepad.update(state, input)
    state.visible = input.touch_active == true
    if not state.visible then
        state.finger, state.held = nil, {}
        state.base_x, state.base_y = stick.default_x, stick.default_y
        state.knob_x, state.knob_y = stick.default_x, stick.default_y
        return ""
    end
    local touches = input.touches or {}
    local by_id = {}
    for _, touch in ipairs(touches) do by_id[touch.id] = touch end

    local owner = state.finger and by_id[state.finger]
    if not owner then
        state.finger = nil
        for _, touch in ipairs(touches) do
            if inside_zone(stick.zone, touch.x, touch.y) then
                state.finger, owner = touch.id, touch
                break
            end
        end
        if owner then
            state.base_x = clamp(owner.x, stick.radius, 794 - stick.radius)
            state.base_y = clamp(owner.y, stick.radius, 550 - stick.radius)
        end
    end
    local held = {}
    if owner then
        local dx, dy = owner.x - state.base_x, owner.y - state.base_y
        local distance = math.sqrt(dx * dx + dy * dy)
        if distance > 0 then
            local travel = math.min(distance, stick.travel)
            state.knob_x, state.knob_y = state.base_x + dx / distance * travel, state.base_y + dy / distance * travel
        else
            state.knob_x, state.knob_y = state.base_x, state.base_y
        end
        if distance >= stick.deadzone then
            if dx / distance > stick.threshold then held.r = true end
            if dx / distance < -stick.threshold then held.l = true end
            if dy / distance > stick.threshold then held.d = true end
            if dy / distance < -stick.threshold then held.u = true end
        end
        -- Crossing run_distance arms a run pulse for l/r; returning past run_rearm_distance
        -- re-arms it (short of the edge, to avoid jitter).
        local run_letter = held.r and "r" or (held.l and "l" or nil)
        if run_letter and distance >= stick.run_distance then
            if state.run_armed ~= run_letter then
                state.run_armed, state.run_pulse, state.run_pulse_step = run_letter, run_pulse_steps, 1
            end
        elseif distance <= stick.run_rearm_distance then
            state.run_armed = nil
        end
    else
        state.base_x, state.base_y = stick.default_x, stick.default_y
        state.knob_x, state.knob_y = stick.default_x, stick.default_y
        state.run_armed, state.run_pulse = nil, nil
    end
    if state.run_pulse then
        held[state.run_armed] = state.run_pulse[state.run_pulse_step]
        state.run_pulse_step = state.run_pulse_step + 1
        if state.run_pulse_step > #state.run_pulse then state.run_pulse = nil end
    end

    -- Any finger over a button circle holds it; multiple fingers can hold multiple buttons
    -- plus the stick.
    for _, button in ipairs(buttons) do
        for _, touch in ipairs(touches) do
            if inside_circle(button.x, button.y, button.radius, touch.x, touch.y) then
                held[button.action] = true
                break
            end
        end
    end
    state.held = held
    local letters = {}
    for _, letter in ipairs({"u", "d", "l", "r", "c", "b", "f"}) do
        if held[letter] then letters[#letters + 1] = letter end
    end
    return table.concat(letters)
end

-- A circle approximated by horizontal strips (no rounded/alpha-blended primitive exists),
-- matching the game's chunky pixel look.
local circle_rows = {0.34, 0.63, 0.83, 0.96, 1, 1, 0.96, 0.83, 0.63, 0.34}
local function disc(context, cx, cy, radius, red, green, blue)
    -- Draw coordinates must be integers, so the finger-driven centre is rounded once here.
    cx, cy = math.floor(cx + 0.5), math.floor(cy + 0.5)
    local step = radius * 2 / #circle_rows
    for index, half_fraction in ipairs(circle_rows) do
        local top = cy - radius + (index - 1) * step
        local half = math.floor(half_fraction * radius)
        if half > 0 then context.fill(cx - half, math.floor(top), half * 2, math.ceil(step), red, green, blue) end
    end
end

-- Call only while state.visible; the caller hides it while paused or off the match screen.
function gamepad.draw(state, context)
    if not state.visible then return end
    local engaged = state.finger ~= nil
    local base_color = engaged and {55, 90, 130} or {48, 48, 56}
    local knob_color = engaged and {150, 190, 235} or {95, 95, 105}
    disc(context, state.base_x, state.base_y, stick.radius, outline[1], outline[2], outline[3])
    disc(context, state.base_x, state.base_y, stick.radius - 6, base_color[1], base_color[2], base_color[3])
    disc(context, state.knob_x, state.knob_y, 24, outline[1], outline[2], outline[3])
    disc(context, state.knob_x, state.knob_y, 18, knob_color[1], knob_color[2], knob_color[3])
    for _, button in ipairs(buttons) do
        local color = (state.held[button.action] and button.pressed) or button.fill
        disc(context, button.x, button.y, button.radius, outline[1], outline[2], outline[3])
        disc(context, button.x, button.y, button.radius - 6, color[1], color[2], color[3])
        font.draw(context, button.label, button.x - #button.label * 4, button.y - 8)
    end
end

return gamepad
