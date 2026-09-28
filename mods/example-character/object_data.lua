-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

return function(base)
    local load = base.load
    local fighter_path = "mods/example-character/fighter.txt"
    local copy
    function base.load(path)
        if path ~= fighter_path then return load(path) end
        if not copy then
            local source
            for _, fighter in ipairs(require("base/game/catalog").load()) do
                if fighter.id >= 1 and fighter.id < 30 then source = fighter; break end
            end
            assert(source, "example-character needs a base fighter")
            local original = load(source.path)
            local authored = load(path)
            copy = {}
            for key, value in pairs(original) do copy[key] = value end
            copy.name, copy.head, copy.small = authored.name:gsub("_", " "), authored.head, authored.small
            copy.sheets = authored.sheets
            copy.movement = {}
            for key, value in pairs(original.movement) do copy.movement[key] = value end
            copy.movement.walking_speed = authored.movement.walking_speed
            local voice = authored.frames[5].sound
            copy.weapon = {}
            for key, value in pairs(original.weapon) do copy.weapon[key] = value end
            for _, key in ipairs({"hit_sound", "drop_sound", "broken_sound"}) do
                if copy.weapon[key] then copy.weapon[key] = voice end
            end
            copy.frames = {}
            for index, frame in pairs(original.frames) do
                local own = {}
                for key, value in pairs(frame) do own[key] = value end
                own.picture = 0
                if own.sound or index == 5 then own.sound = voice end
                copy.frames[index] = own
            end
        end
        return copy
    end
    return base
end
