// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include "openlf2/core/result.hpp"
#include "openlf2/core/script_value.hpp"
#include "openlf2/ports/resources.hpp"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace openlf2::stage_data {
inline constexpr std::size_t max_stages = 60;
inline constexpr std::size_t max_phases = 100;
inline constexpr std::size_t max_entries = 60;

enum class EntryKind : std::int32_t { normal = 0, soldier = 1, boss = 2 };

// Defaults are the values `id:` of a stage writes into all of its phases and entries.
struct Entry {
    std::int32_t id = -1;
    std::int32_t x = 500; // also set to bound + 80 by every later `bound:` of the phase
    std::int32_t hp = 500;
    std::int32_t times = 1; // `<soldier>` sets 50
    std::int32_t reserve = 0;
    std::int32_t join = 0;
    std::int32_t join_reserve = 0;
    std::int32_t act = 9;
    std::int32_t y = 0;
    double ratio = 0.0;
    EntryKind kind = EntryKind::normal;
};

struct Phase {
    std::int32_t bound = -1;
    std::string music;
    std::int32_t when_clear_goto_phase = -1;
    std::vector<Entry> entries; // entries opened by `id:`, in file order
};

struct Stage {
    std::vector<Phase> phases; // phase count as the original's +2000 field
};

struct StageList {
    // Stages the file never defines keep the original's phase count -1 (absent here).
    std::array<std::optional<Stage>, max_stages> stages;
    std::vector<std::string> diagnostics;
};

Result<StageList> parse(std::string_view text);
Result<StageList> load(const ResourceSource& resources, std::string_view path);
// Script-facing table.
ScriptValue to_script_value(const StageList& list);
}
