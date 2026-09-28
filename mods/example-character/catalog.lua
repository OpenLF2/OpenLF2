-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- A selectable fighter with its own record ID, based on an existing art set.
return function(base)
    local fighter_id = 60
    local fighter_path = "mods/example-character/fighter.txt"
    local load, objects = base.load, base.objects
    local added_fighter, added_object = false, false

    function base.load()
        local fighters = load()
        if not added_fighter then
            assert(added_object or not objects().by_id[fighter_id],
                "example-character ID is already in use")
            local header = assert(engine.read_resource(fighter_path):match("<bmp_begin>(.-)<bmp_end>"))
            local name = assert(header:match("name:%s*(%S+)")):gsub("_", " ")
            local portrait = assert(header:match("head:%s*(%S+)"))
            fighters[#fighters + 1] = {id = fighter_id,
                record = #objects().list - (added_object and 1 or 0),
                name = name, portrait = portrait,
                path = fighter_path, hidden = false}
            added_fighter = true
        end
        return fighters
    end

    function base.objects()
        local catalog = objects()
        if not added_object then
            assert(not catalog.by_id[fighter_id], "example-character ID is already in use")
            local entry = {id = fighter_id, kind = 0, path = fighter_path}
            catalog.list[#catalog.list + 1] = entry
            catalog.by_id[fighter_id] = entry
            added_object = true
        end
        return catalog
    end

    return base
end
