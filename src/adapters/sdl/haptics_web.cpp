// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/detail/sdl/haptics.hpp"

#include <emscripten.h>

namespace openlf2 {
namespace {
EM_JS(void, vibrate_phone, (int duration_ms), {
    try {
        if (typeof navigator.vibrate === 'function') navigator.vibrate(duration_ms);
    } catch (_) {}
});
}

void trigger_phone_haptics(int strength) {
    if (strength > 0) vibrate_phone(150 * strength / 100);
}
}
