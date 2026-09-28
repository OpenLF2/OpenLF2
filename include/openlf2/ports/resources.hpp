// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include "openlf2/core/result.hpp"
#include <string_view>

namespace openlf2 {
class ResourceSource {
public:
    virtual ~ResourceSource() = default;
    virtual Result<Bytes> read(std::string_view path) const = 0;
};
}
