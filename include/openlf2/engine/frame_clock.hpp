// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
#include <algorithm>
#include <cstdint>

namespace openlf2 {
class FrameClock {
public:
    static constexpr std::uint32_t normal_step = 33; // about 30.3 frames per second
    static constexpr std::uint32_t fast_step = 3;    // F5 fast mode, requested by scripts
    static constexpr std::uint32_t max_lag = 100;
    static constexpr std::uint32_t max_sleep = 5;

    explicit FrameClock(std::uint32_t now) : scheduled_(now) {}

    // At most one frame per call, as each original loop iteration runs at most one.
    [[nodiscard]] bool due(std::uint32_t now, bool fast = false) {
        const auto step = fast ? fast_step : normal_step;
        if (now - scheduled_ <= step) return false;
        if (now - scheduled_ > max_lag) scheduled_ = now - max_lag;
        scheduled_ += step;
        return true;
    }
    // Milliseconds the original sleeps before checking again (0 means poll immediately).
    [[nodiscard]] std::uint32_t sleep_ms(std::uint32_t now, bool fast = false) const {
        const auto step = fast ? fast_step : normal_step;
        const auto wait = static_cast<std::int32_t>(scheduled_ - now + step);
        return wait <= 0 ? 0 : std::min(static_cast<std::uint32_t>(wait), max_sleep);
    }

private:
    std::uint32_t scheduled_;
};
}
