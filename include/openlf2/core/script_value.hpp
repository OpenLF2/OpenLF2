// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace openlf2 {
// Immutable data handed to scripts. Adapters translate it into their own representation;
// keys keep insertion order so conversion is deterministic.
struct ScriptField;
using ScriptKey = std::variant<std::int32_t, std::string>;
using ScriptTable = std::vector<ScriptField>;
struct ScriptValue {
    std::variant<std::monostate, bool, std::int32_t, double, std::string, ScriptTable> value;
};
struct ScriptField {
    ScriptKey key;
    ScriptValue value;
};

inline void set_field(ScriptTable& table, ScriptKey key, ScriptValue value) {
    table.push_back(ScriptField{std::move(key), std::move(value)});
}
}
