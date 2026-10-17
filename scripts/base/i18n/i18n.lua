-- SPDX-License-Identifier: MIT
-- Copyright (c) 2026 OpenLF2 contributors

-- Translation of the texts the scripts draw and of the text baked into the original's bitmaps. A
-- language is a module "base/i18n/<id>" returning a table of text -> translation; the scripts pass their
-- English text to i18n.tr, and the bitmap text is keyed by the original's bilingual strings (see
-- baked.lua). A text a language has no entry for is shown as it is; "" erases it.
--
-- To add a language: create its module (copy german.lua), list it in package.manifest, add its id to
-- `languages` below, and give every language module an entry for the new id (the dropdown shows each
-- language's name in the current language).
local i18n = {}

-- The selectable languages after "default", in dropdown order. "default" is the original's behavior:
-- bitmap font, English scripts and the bitmaps as drawn. Every other language is drawn with TrueType
-- fonts (the host's text renderer), so it only takes effect when the host offers it (see i18n.host).
i18n.languages = {"english", "chinese", "german"}

local selected, antialias, host_text = "default", true, false
local strings = {}
local baked
local group_fit = {}

function i18n.is_language(id)
    if id == "default" then return true end
    for _, language in ipairs(i18n.languages) do
        if language == id then return true end
    end
    return false
end

-- Whether the host can draw TrueType text (the "t" capability); called every frame.
function i18n.host(supported) host_text = supported == true end

-- Selects the language and whether the TrueType text is antialiased.
function i18n.use(language, font_antialiasing)
    selected, antialias = language, font_antialiasing ~= false
    strings = language ~= "default" and require("base/i18n/" .. language) or {}
    group_fit = {}
end

-- True when text is drawn with TrueType fonts.
function i18n.active() return selected ~= "default" and host_text end
function i18n.antialias() return antialias end

-- The translation of `text`, with string.format applied when arguments follow. An entry that is "" only
-- erases baked bitmap text (see paint_baked); for script text it counts as no translation.
function i18n.tr(text, ...)
    local translated = i18n.active() and strings[text]
    if not translated or translated == "" then translated = text end
    if select("#", ...) > 0 then return string.format(translated, ...) end
    return translated
end

-- A dropdown entry for a language: its id, then its name in the current language in parentheses
-- ("german (Deutsch)"); just the id while the default language is in use.
function i18n.language_label(id)
    local name = i18n.tr(id)
    return name == id and id or id .. " (" .. name .. ")"
end

-- The longest start of `text` of at most `limit` bytes that does not cut a UTF-8 character.
function i18n.clip(text, limit)
    if #text <= limit then return text end
    local cut = limit
    while cut > 0 and text:byte(cut + 1) >= 0x80 and text:byte(cut + 1) < 0xc0 do cut = cut - 1 end
    return text:sub(1, cut)
end

-- Width of one character in em: roughly DejaVu Sans for Latin, a full em for CJK.
local function em(character)
    local code = character:byte(1)
    if #character >= 3 then return 1 end
    if code >= 0x41 and code <= 0x5a then return 0.72 end
    if code >= 0x30 and code <= 0x39 then return 0.64 end
    if code >= 0x61 and code <= 0x7a then return 0.56 end
    return code == 0x20 and 0.32 or 0.5
end

-- Estimated width in pixels of TrueType `text` at font `size`: the host draws text but never reports
-- its size back, so scripts guess.
function i18n.width(text, size)
    local width = 0
    for character in text:gmatch("[\1-\127\194-\244][\128-\191]*") do width = width + em(character) * size end
    return width
end

-- A screen that draws bitmaps in its own way can replace the text of one baked string for as long as it draws:
-- override(key, {text, align, tx, room}) (anchor and room in sheet pixels, both optional), override(key, nil) ends it.
local overrides = {}
function i18n.override(key, replacement) overrides[key] = replacement end

local function channel(color, shift) return math.floor(color / shift) % 256 end

-- The translation to paint for `region`, or nil when it stays as drawn.
local function replacement(region)
    local text = strings[region.text]
    return text ~= region.text and text or nil
end

-- How much of its size a region's text may keep to fit the room it has.
local function fit(region, text)
    return math.min(1, region.room / i18n.width(text, region.size))
end

-- Lines of one paragraph or list (`group`) keep one size: the smallest any of them needs. Computed once
-- per sheet and language.
local function group_scale(resource, regions, region)
    local key = resource .. region.group
    local scale = group_fit[key]
    if scale then return scale end
    scale = 1
    for _, member in ipairs(regions) do
        local text = member.group == region.group and replacement(member)
        if text and text ~= "" then scale = math.min(scale, fit(member, text)) end
    end
    group_fit[key] = scale
    return scale
end

-- Redraws what lies behind a color-keyed sheet at the screen rectangle: the viewport color and the slice
-- of the backdrop picture (`scene`, kept by the frame loop) under it.
local function restore_behind(context, scene, x, y, w, h)
    context.fill(x, y, w, h, scene.red or 0, scene.green or 0, scene.blue or 0)
    local picture = scene.backdrop
    local size = picture and baked.backdrops[picture]
    if not size then return end
    local left, top = math.max(x, scene.backdrop_x), math.max(y, scene.backdrop_y)
    local right = math.min(x + w, scene.backdrop_x + size[1])
    local bottom = math.min(y + h, scene.backdrop_y + size[2])
    if right > left and bottom > top then
        context.sprite(picture, {left - scene.backdrop_x, top - scene.backdrop_y, right - left, bottom - top}, left, top, true)
    end
end

-- Replaces the text baked into the part `source` ({x, y, w, h}, nil for all) of the sheet `resource`,
-- drawn at (x, y): every region of baked.lua in view whose translation differs is painted over and its
-- translation drawn instead. Black is the color key of a keyed sheet (the picture behind shows through),
-- so a black region is covered by redrawing that picture; any other background by a fill of its color.
-- `scene` is what the frame has drawn so far (viewport color, backdrop). Called for each sprite while a
-- language is active.
function i18n.paint_baked(context, scene, resource, source, x, y, keyed)
    baked = baked or require("base/i18n/baked")
    local regions = baked[resource]
    if not regions then return end
    local left, top = source and source[1] or 0, source and source[2] or 0
    local right = source and left + source[3] or math.huge
    local bottom = source and top + source[4] or math.huge
    local dx, dy = x - left, y - top
    -- Every cover first, then the texts: a text may reach over the cover of the region below it.
    local texts = {}
    for _, region in ipairs(regions) do
        local over = overrides[region.text]
        local text = over and over.text or replacement(region)
        -- A region counts as in view when the sprite shows it but for a pixel or two of its tight box.
        if text and region.x >= left - 2 and region.y >= top - 2 and region.x + region.w <= right + 2
            and region.y + region.h <= bottom + 2 then
            local pad = region.pad
            local covered_left, covered_top = math.max(region.x - pad, left), math.max(region.y - pad, top)
            local covered_right = math.min(region.x + region.w + pad + (region.ex or 0), right)
            local covered_bottom = math.min(region.y + region.h + pad, bottom)
            local back = region.back
            if keyed and back == 0 then
                restore_behind(context, scene, covered_left + dx, covered_top + dy, covered_right - covered_left,
                    covered_bottom - covered_top)
            else
                context.fill(covered_left + dx, covered_top + dy, covered_right - covered_left, covered_bottom - covered_top,
                    channel(back, 65536), channel(back, 256), channel(back, 1))
            end
            if text ~= "" then
                local scale = over and math.min(1, (over.room or region.room) / i18n.width(text, region.size))
                    or region.group and math.min(fit(region, text), group_scale(resource, regions, region)) or fit(region, text)
                local size = math.max(8, math.floor(region.size * scale))
                local align, anchor = over and over.align or region.align, over and over.tx or region.tx
                -- Centered text that would pass the left edge of the sheet starts at that edge instead.
                if align == "center" and anchor - i18n.width(text, size) / 2 < left then align, anchor = "left", left end
                texts[#texts + 1] = {text, anchor + dx, region.ty + dy + math.floor((region.size - size) * 0.6), align, size,
                    region.color, region.outline}
            end
        end
    end
    for _, t in ipairs(texts) do context.text(t[1], t[2], t[3], t[4], t[5], t[6], t[7], nil, antialias) end
end
return i18n
