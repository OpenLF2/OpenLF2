-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

local constants = require("base/game/constants")
local i18n = require("base/i18n/i18n")
local screen = {}

local sheet = "pe/ending"
local frames = {[0] = {0, 0, 402, 66}, {0, 67, 402, 91}, {0, 159, 402, 156}, {0, 316, 419, 169},
    {0, 486, 419, 65}, {403, 53, 15, 13}}

-- The text of each page in English, which is also the key a language translates; the bitmap holds it in two languages
-- (Chinese lines and English lines). A translated language draws its page as one wrapped text in one font.
local page_texts = {
    [0] = "Congratulations, you have finished the stage mode.\nTry challenging a higher level!",
    "Congratulations, you have finished the stage mode.\nTo reveal the secret characters, just type \"lf2.net\" at character selection menu!",
    "Thank you very much for your support in LF2.\nDo you know? It has always been our dreams to create a global popular game, and seem to me now, our goal has been achieved! We hope LF2 will leave a print in every player's heart! After 10 years, 20 years ... hope you all will still remember the game!",
    "After LF2 is finished, we shall all go back to our job duties...\nMarti will try establishing his own career, while Starsky will continue his studies in the US (UCLA). It is an exceptionally fine incidence to meet somebody who will strive for the same goal. Hope there will be one day we can have opportunity to work together again, to create a more fantastic game!",
    "Thank you again for all your support. Long live LF2!\n\nLF2 Design Team\nSep 2002",
}

function screen.create(difficulty)
    return {difficulty = difficulty, page = 0, reveal = 0, closing = 0, blink = 0, display = {},
        previous_attack = {true, true, true, true, true, true, true, true}}
end

local function cdiv(a, b)
    local q = a / b
    if q >= 0 then return math.floor(q) end
    return math.ceil(q)
end

function screen.update(state, held, context)
    local display = {}
    state.display = display
    state.blink = (state.blink + 1) % 10
    if state.difficulty == constants.difficulties.normal or state.difficulty == constants.difficulties.easy then
        if state.page == 1 then state.page = 2 end
    elseif state.page == 0 then
        state.page = 1
    end
    local frame = frames[state.page]
    local x, y = cdiv(800 - frame[3], 2), cdiv(520 - frame[4], 2)
    display[#display + 1] = {i18n.active() and "page" or "sprite", frame, x, y, state.page}
    local fresh = false
    for player = 0, 7 do
        local attack = (held[player] or ""):find("c", 1, true) ~= nil
        if attack and not state.previous_attack[player + 1] then fresh = true end
        state.previous_attack[player + 1] = attack
    end
    local row = y
    local bars
    if state.reveal < 13 then
        state.reveal = state.reveal + 1
        for _ = 1, 13 do
            display[#display + 1] = {"fill", 0, row, constants.screen.width, 13 - state.reveal}
            row = row + 13
        end
        bars = state.closing
    else
        bars = state.closing
        if bars == 0 then
            if fresh then
                bars = 1
                state.closing = bars
            end
        elseif bars > 0 then
            for _ = 1, 13 do
                display[#display + 1] = {"fill", 0, row, constants.screen.width, bars}
                row = row + 13
            end
            bars = bars + 1
            state.closing = bars
            if bars >= 13 then
                state.page = state.page + 1
                state.reveal, bars, state.closing = 0, 0, 0
            end
        end
    end
    if state.blink < 5 and state.reveal == 13 and bars == 0 then
        display[#display + 1] = {"sprite", frames[5], x + frame[3], y + frame[4]}
    end
    if state.page == 5 then context.action("title") end
end

function screen.draw(state, context)
    context.viewport(constants.screen.width, constants.screen.height, 0, 0, 0)
    for _, command in ipairs(state.display) do
        if command[1] == "sprite" then context.sprite(sheet, command[2], command[3], command[4], false)
        elseif command[1] == "page" then
            -- The page's area on black, the text wrapped to its width and sized to its height.
            local frame, x, y = command[2], command[3], command[4]
            context.fill(x, y, frame[3], frame[4], 0, 0, 0)
            local lines, size, distance = i18n.fit(i18n.tr(page_texts[command[5]] or ""), frame[3] - 4, frame[4], 15, 10)
            for index, line in ipairs(lines) do
                if line ~= "" then
                    context.text(line, x + 2, y + (index - 1) * distance, "left", size, 0xffffff, nil, nil, i18n.antialias())
                end
            end
        else context.fill(command[2], command[3], command[4], command[5], 0, 0, 0) end
    end
end

function screen.describe(state)
    return string.format("ending_page=%d reveal=%d closing=%d", state.page, state.reveal, state.closing)
end
return screen
