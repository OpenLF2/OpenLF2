// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include "openlf2/core/script_value.hpp"
#include <string>
#include <string_view>

namespace openlf2 {
// JSON (RFC 8259). Objects/arrays map to tables (string keys / 1..n keys); rejects duplicate
// keys, >32 levels of nesting, and inputs over 1 MiB.
Result<ScriptValue> parse_json(std::string_view text);
// Inverse of parse_json; rejects mixed-key tables and non-finite numbers.
Result<std::string> write_json(const ScriptValue& value);
}
