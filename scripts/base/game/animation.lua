-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

local constants = require("base/game/constants")
local blink_defeated_frames = constants.blink.defeated_frames
local blink_frozen_tail_frames = constants.blink.frozen_tail_frames
local blink_get_up_frames = constants.blink.get_up_frames
local character_frame_broken_defend = constants.character_frames.broken_defend
local character_frame_defend = constants.character_frames.defend
local character_frame_ice = constants.character_frames.ice
local difficulty_easy = constants.difficulties.easy
local facing_left = constants.facings.left
local facing_right = constants.facings.right
local fighter_bat = constants.fighter_ids.bat
local frame_limit = constants.frame_limit
local frame_selector_return_to_standing = constants.frame_selectors.return_to_standing
local state_flying_heavy_item = constants.frame_states.flying_heavy_item
local state_frozen = constants.frame_states.frozen
local state_lying = constants.frame_states.lying
local state_standing = constants.frame_states.standing
local input_defend_lock_frames = constants.input.defend_lock_frames
local mode_battle = constants.modes.battle
local mode_stage = constants.modes.stage
local team_enemies = constants.teams.enemies
local cpoint_kinds = constants.cpoint_kinds
local record_kinds = constants.record_kinds
local sounds = require("base/game/sounds")
local animation = {}
local jump_frame, standing_frame = constants.character_frames.jump + 2, constants.character_frames.standing

function animation.advance(match, value, index)
    local record = value.record
    local frame = record.frame
    if value.hit_lag ~= 0 and record.kind ~= record_kinds.ball then return end
    if value.timer_ec > 0 then value.timer_ec = value.timer_ec - 1 end
    if value.weapon < 0 then return end
    if frame(value.frame).cpoint.kind == cpoint_kinds.caught then return end
    if record.kind == record_kinds.ball then
        -- Attack objects drain their own health by hit_a per frame, then go to hit_d.
        local drain = frame(value.frame).hits.a
        if drain > 0 then
            value.hp = value.hp - drain
            if value.hp <= 0 then
                value.hp = 0
                value.frame = frame(value.frame).hits.d
            end
        end
    end
    if value.blink > 0 then value.blink = value.blink - 1 end
    if value.blink < 0 then value.blink = value.blink + 1 end
    if value.timer_b0 > 0 then value.timer_b0 = value.timer_b0 - 1 end
    if value.timer_b8 > 0 then value.timer_b8 = value.timer_b8 - 1 end
    if value.super_window > 0 then value.super_window = value.super_window - 1 end
    if value.frame ~= value.previous_frame then
        -- A frame entered since the last advance plays its sound.
        sounds.item(match, value.x_int, frame(value.frame).sound)
        value.wait_counter = 0
    end
    value.wait_counter = value.wait_counter + 1
    if record.kind >= 0 and frame(value.frame).state == state_standing and value.y_int < 0 then value.frame = jump_frame end
    if record.kind == record_kinds.heavy_item and frame(value.frame).state == state_flying_heavy_item and value.y_int == 0
       and value.vx < 0.1 and -0.1 < value.vx then
        value.frame = 20
    end
    if frame(value.frame).state == state_lying and value.hp <= 0 then
        -- Defeated and lying: stay down; non-player fighters start blinking out.
        if (value.follow >= 0 or value.team == team_enemies or index > 19) and value.blink < 1 then value.blink = blink_defeated_frames end
        value.wait_counter = 0
    end
    if frame(value.frame).state == state_flying_heavy_item then value.facing = value.vx > 0 and facing_right or facing_left end
    if value.wait_counter <= frame(value.frame).wait then return animation.tail(value) end
    value.wait_counter = 0
    if frame(value.frame).next == 0 then return animation.tail(value) end
    value.frame = frame(value.frame).next
    if value.frame < 0 then
        value.facing = 1 - value.facing
        value.frame = -value.frame
    end
    local airborne_landing = false
    if value.frame == frame_selector_return_to_standing then
        if value.y_int == 0 or record.kind ~= record_kinds.character then value.frame = standing_frame
        else
            airborne_landing = true
            value.frame = jump_frame
        end
    end
    if value.frame < 0 or value.frame >= frame_limit then return end
    -- Getting up after lying gives a short invulnerable blink (not for some stage enemies).
    if frame(value.previous_frame).state == state_lying and frame(value.frame).state ~= state_frozen
       and (((value.team ~= team_enemies and value.battle_side == 0) or match.difficulty ~= difficulty_easy)
       and (((match.mode ~= mode_stage and match.mode ~= mode_battle)
             or (value.team ~= team_enemies and value.battle_side == 0))
            or (math.floor(record.id / 10) ~= 3 or record.id == fighter_bat))) then
        value.blink = blink_get_up_frames
    end
    if value.frame == jump_frame and not airborne_landing then
        local movement = record.movement
        value.vy = movement.jump_height or 0
        if value.keys.right and not value.keys.left then value.vx = movement.jump_distance or 0
        elseif value.keys.left and not value.keys.right then value.vx = -(movement.jump_distance or 0) end
        if not value.keys.up and value.keys.down then value.vz = movement.jump_distancez or 0
        elseif value.keys.up and not value.keys.down then value.vz = -(movement.jump_distancez or 0) end
    end
    sounds.item(match, value.x_int, frame(value.frame).sound)
    local current = frame(value.frame)
    if current.mp < 0 and match.running then
        -- Continuous mp cost: without enough mp the move ends at hit_d.
        if value.mp < current.mp then value.frame = current.hits.d
        else
            value.mp = value.mp + current.mp
            value.mp_spent = value.mp_spent - current.mp
        end
        current = frame(value.frame)
        if current.hits.d > 0 then
            local keys = value.keys
            if keys.left and not keys.right and value.y_int == 0 and value.facing == facing_right then value.frame = current.hits.d end
            if not keys.left and keys.right and value.y_int == 0 and value.facing == facing_left then value.frame = current.hits.d end
        end
    end
    return animation.tail(value)
end

function animation.tail(value)
    if value.frame == character_frame_defend or value.frame == character_frame_broken_defend + 2 then value.defend_lock = input_defend_lock_frames end
    if value.frame == character_frame_ice + 2 then value.blink = blink_frozen_tail_frames end
    value.previous_frame = value.frame
end
return animation
