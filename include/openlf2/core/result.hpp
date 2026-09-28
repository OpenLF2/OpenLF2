// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include <expected>
#include <string>
#include <vector>
#include <utility>

namespace openlf2 {
using Bytes = std::vector<char>;
enum class ErrorCode { io, format, unsupported, dependency, script, platform, limit };
struct Error {
    ErrorCode code;
    std::string message;
};
template <typename T> using Result = std::expected<T, Error>;
inline auto fail(ErrorCode code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}
}
