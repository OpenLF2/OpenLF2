-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

local constants = require("base/game/constants")
local defense_normal = constants.defense.normal
local state_airborne_item = constants.frame_states.airborne_item
local state_burning = constants.frame_states.burning
local state_falling = constants.frame_states.falling
local state_frozen = constants.frame_states.frozen
local state_louis_dash_attack = constants.frame_states.louis_dash_attack
local state_rowing = constants.frame_states.rowing
local state_thrown_item = constants.frame_states.thrown_item
local gravity_baseball = constants.gravity.baseball
local gravity_default = constants.gravity.default
local gravity_drink = constants.gravity.drink
local gravity_thrown_boomerang = constants.gravity.thrown_boomerang
local gravity_thrown_hoe = constants.gravity.thrown_hoe
local gravity_thrown_item = constants.gravity.thrown_item
local gravity_thrown_knife = constants.gravity.thrown_knife
local item_id_boomerang = constants.item_ids.boomerang
local item_id_hoe = constants.item_ids.hoe
local item_id_knife = constants.item_ids.knife
local landing_bounce_speed_x_threshold = constants.landing.bounce_speed_x_threshold
local landing_bounce_speed_y_threshold = constants.landing.bounce_speed_y_threshold
local landing_bounce_vy = constants.landing.bounce_vy
local landing_frozen_bounce_speed_y_threshold = constants.landing.frozen_bounce_speed_y_threshold
local landing_frozen_impact_damage = constants.landing.frozen_impact_damage
local landing_max_bounce_speed_x = constants.landing.max_bounce_speed_x
local object_broken_weapon = constants.object_ids.broken_weapon
local character_frames = constants.character_frames
local cpoint_kinds = constants.cpoint_kinds
local record_kinds = constants.record_kinds
local sounds = require("base/game/sounds")
local physics = {}

-- C (int) conversion of a double truncates toward zero.
local function truncate(value)
    if value >= 0 then return math.floor(value) end
    return math.ceil(value)
end
physics.truncate = truncate

local function stop(value)
    value.vy = 0.0
    value.wait_counter = 0
    value.vx = value.vx * 0.5
end

local function landed(value, index)
    local frame = value.record.frame
    value.y = 0.0
    value.vy = 0.0
    value.vx = value.vx / 3.0
    local state = frame(index).state
    if state == state_louis_dash_attack then value.frame = 94
    elseif index == character_frames.jump + 2 or state == state_rowing then value.frame = character_frames.crouch
    else value.frame = character_frames.crouch_alternate end
    value.wait_counter = 0
end

-- Kind-0 (character) landing and falling rules.
local function character_ground(value, match)
    local frame = value.record.frame
    if value.y > 0.0001 and value.vy > 0.0001 and frame(value.frame).state == state_frozen then
        if value.vy > landing_frozen_bounce_speed_y_threshold or value.vx > landing_bounce_speed_x_threshold or value.vx < -landing_bounce_speed_x_threshold then
            if value.armor == 0 then value.hp = value.hp - landing_frozen_impact_damage
            else value.hp = value.hp + truncate(-landing_frozen_impact_damage * defense_normal / value.armor) end
            value.y = 0.0
            value.vy = landing_bounce_vy
            if value.vx > landing_max_bounce_speed_x then value.vx = landing_max_bounce_speed_x end
            if value.vx < -landing_max_bounce_speed_x then value.vx = -landing_max_bounce_speed_x end
            value.frame = 185
        else
            value.y = 0.0
            value.vy = 0.0
            value.vx = value.vx / 3.0
        end
    end
    if value.y > 0.0 and value.vy == 0.0 and value.frame == character_frames.jump + 2 then
        return landed(value, character_frames.jump + 2)
    end
    if value.y > 0.0001 and value.vy > 0.0001 then
        local state = frame(value.frame).state
        if state ~= state_falling and state ~= state_burning then return landed(value, value.frame) end
        sounds.effect(match, value.x_int, 6)
        -- Landing from a fall (12) or a fire/ice fall (18): bounce or lie down, taking fall damage.
        local damage = value.fall_damage
        if damage ~= 0 then
            if damage < 0 then value.fall_damage = -damage end
            damage = value.fall_damage
            if value.armor > 0 then damage = truncate(damage * 100 / value.armor) end
            value.hp = value.hp - damage
            value.dark_hp = value.dark_hp - damage
            value.fall_damage = 0
        end
        if value.vy > landing_bounce_speed_y_threshold or value.vx > landing_bounce_speed_x_threshold or value.vx < -landing_bounce_speed_x_threshold or frame(value.frame).state == state_burning then
            value.y = 0.0
            value.vy = landing_bounce_vy
            if value.vx > landing_max_bounce_speed_x then value.vx = landing_max_bounce_speed_x end
            if value.vx < -landing_max_bounce_speed_x then value.vx = -landing_max_bounce_speed_x end
            if value.frame < 186 or frame(value.frame).state == state_burning then value.frame = 185
            else value.frame = 191 end
        else
            value.y = 0.0
            value.vy = 0.0
            value.wait_counter = 0
            value.vx = value.vx / 3.0
            value.frame = value.frame >= 186 and character_frames.lying + 1 or character_frames.lying
        end
    end
end

function physics.step(match, value)
    local record = value.record
    local frame = record.frame
    if value.hit_lag ~= 0 then
        -- Hit lag counts toward zero instead of moving.
        if value.hit_lag > 0 then value.hit_lag = value.hit_lag - 1 end
        if value.hit_lag >= 0 then return end
        value.hit_lag = value.hit_lag + 1
        return
    end
    if value.weapon < 0 then return end
    if frame(value.frame).cpoint.kind == cpoint_kinds.caught then return end
    if not ((value.vx > 0.0 and value.blocked_right) or (value.vx < 0.0 and value.blocked_left)) then
        value.x = value.x + value.vx
    end
    if record.kind == record_kinds.baseball or record.id == item_id_knife then value.x = value.vx * 0.2 + value.x end
    if record.id == item_id_hoe then value.x = value.x - 0.2 * value.vx end
    if not ((value.vz > 0.0 and value.blocked_down) or (value.vz < 0.0 and value.blocked_up)) then
        value.z = value.z + value.vz
    end
    value.blocked_up, value.blocked_down, value.blocked_left, value.blocked_right = false, false, false, false
    if record.kind == record_kinds.ball and frame(value.frame).hits.j > 0 then
        value.z = (frame(value.frame).hits.j - 50) + value.z
    end
    if value.y_int >= 0 then
        -- Ground friction of one unit per frame.
        if value.vx > 0.0001 then
            value.vx = value.vx - 1.0
            if value.vx < 0.0001 then value.vx = 0.0 end
        end
        if value.vx < -0.0001 then
            value.vx = value.vx + 1.0
            if 0.0001 < value.vx then value.vx = 0.0 end
        end
        if value.vz > 0.0001 then
            value.vz = value.vz - 1.0
            if value.vz < 0.0001 then value.vz = 0.0 end
        end
        if value.vz < -0.0001 then
            value.vz = value.vz + 1.0
            if 0.0001 < value.vz then value.vz = 0.0 end
        end
    end
    if (record.kind == record_kinds.baseball or record.kind == record_kinds.drink) and frame(value.frame).state == state_airborne_item
       and (value.vx > 9.0 or value.vx < -9.0) then
        value.frame = 40
    end
    value.y = value.y + value.vy
    if value.y < -0.0001 then
        -- Airborne: gravity by kind and state.
        local kind = record.kind
        if kind ~= record_kinds.ball then
            if kind == record_kinds.drink then value.vy = value.vy + gravity_drink
            elseif kind == record_kinds.baseball then value.vy = value.vy + gravity_baseball
            elseif frame(value.frame).state == state_thrown_item then
                if record.id == item_id_boomerang then value.vy = value.vy + gravity_thrown_boomerang
                elseif record.id == item_id_knife then value.vy = value.vy + gravity_thrown_knife
                elseif record.id == item_id_hoe then value.vy = value.vy + gravity_thrown_hoe
                else value.vy = value.vy + gravity_thrown_item end
            else
                value.vy = value.vy + gravity_default
            end
        end
        if record.kind == record_kinds.character then
            local index = value.frame
            if frame(index).state == state_falling then
                -- Falling frames follow the vertical speed.
                if index < 185 then
                    if value.vy >= -8.0 then
                        if value.vy >= 1.0 then
                            value.frame = value.vy >= 8.0 and 183 or 182
                        else value.frame = 181 end
                    else value.frame = 180 end
                    if value.fall_damage < 0 then
                        value.frame = (value.vy < 12.0 and match.counter_12 > 5) and 182 or 181
                    end
                elseif index < 191 and index > 185 then
                    if value.vy >= -8.0 then
                        if value.vy >= 1.0 then
                            value.frame = value.vy >= 8.0 and 189 or 188
                        else value.frame = 187 end
                    else value.frame = 186 end
                end
            end
            if frame(value.frame).state == state_burning and value.frame < 205 and value.vy > 1.0 then value.frame = 205 end
        end
    elseif frame(value.frame).cpoint.kind ~= cpoint_kinds.caught then
        local kind = record.kind
        if kind == record_kinds.character then
            character_ground(value, match)
        elseif kind == record_kinds.light_item then
            if not (value.y <= 0.0001 or value.vy <= 0.0001) then
                value.y = 0.0
                value.drop_counter = value.drop_counter - record.data.weapon.drop_hurt
                if value.vy <= 9.9 then
                    if frame(value.frame).state == state_thrown_item then value.frame = 70; stop(value)
                    else value.frame = 60; stop(value) end
                elseif frame(value.frame).state == state_thrown_item then
                    value.frame = 7
                    value.vy = -8.0
                    value.facing = 1 - value.facing
                    value.vx = value.vx * 0.5
                    sounds.item(match, value.x_int, record.data.weapon.drop_sound)
                else
                    value.frame = 60
                    stop(value)
                end
            end
        elseif kind == record_kinds.baseball or kind == record_kinds.drink then
            if not (value.y <= 0.0001 or value.vy <= 0.0001) then
                value.drop_counter = value.drop_counter - record.data.weapon.drop_hurt
                if kind == record_kinds.drink and value.hp < 1 then value.drop_counter = -1 end
                value.y = 0.0
                local state = frame(value.frame).state
                if (value.vy <= 8.5 and value.vx >= -10.0 and value.vx <= 10.0)
                   or (state ~= state_thrown_item and state ~= state_airborne_item) then
                    value.vy = 0.0
                    value.wait_counter = 0
                    value.vx = value.vx * 0.7
                    value.frame = state == state_thrown_item and 70 or 60
                else
                    value.frame = 0
                    value.vy = value.vy * -0.7
                    if value.vy < -10.0 then value.vy = -10.0 end
                    value.vx = value.vx * 0.7
                    sounds.item(match, value.x_int, record.data.weapon.drop_sound)
                end
            end
        elseif kind == record_kinds.heavy_item then
            if value.y > 0.0001 then
                value.drop_counter = value.drop_counter - 1
                value.y = 0.0
                if value.vy > 9.0 then
                    sounds.item(match, value.x_int, record.data.weapon.drop_sound)
                    value.vy = -5.0
                    value.facing = 1 - value.facing
                    value.vx = value.vx * 0.5
                else
                    value.drop_counter = value.drop_counter - record.data.weapon.drop_hurt
                    if value.drop_counter < 0 then value.drop_counter = 0 end
                    value.frame = 20
                    stop(value)
                end
            end
        elseif record.id == object_broken_weapon and value.y > -0.0001 then
            value.y = 0.0
            value.frame = 101
            value.vy = 0.0
            value.wait_counter = 0
            value.vx = 0.0
        end
    end
    value.x_int = truncate(value.x)
    value.y_int = truncate(value.y)
    value.z_int = truncate(value.z)
    if frame(value.frame).state ~= state_falling then value.fall_damage = 0 end
end
return physics
