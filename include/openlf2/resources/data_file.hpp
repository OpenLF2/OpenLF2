// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include "openlf2/ports/resources.hpp"
#include <span>
#include <string_view>

namespace openlf2 {
// Reverses the original data-file encoding (123-byte prefix, rotating subtraction key).
Result<Bytes> decode_data_file(std::span<const char> encoded);
// Reads an original text description like the original game does: names ending in "dat" (any case)
// are decoded first. Rejects NUL/0x1A, whose text-mode CRT handling is not reconstructed.
Result<Bytes> read_data_text(const ResourceSource& resources, std::string_view path, std::size_t limit);
}
