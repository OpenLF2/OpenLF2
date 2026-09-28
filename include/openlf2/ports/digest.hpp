// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <span>

namespace openlf2 {
Result<std::string> sha256(std::span<const char> input);
}
