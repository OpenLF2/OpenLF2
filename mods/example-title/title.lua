-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- Optional mod demonstrating extension without editing base scripts.
return function(base)
    local extended = {}
    for key, value in pairs(base) do extended[key] = value end
    function extended.create(options)
        local state = base.create(options)
        state.selection = 5
        return state
    end
    return extended
end
