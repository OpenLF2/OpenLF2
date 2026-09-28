// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Platforms with no phone vibration motor (desktop, consoles); controller rumble still works.
#include "openlf2/detail/sdl/haptics.hpp"

namespace openlf2 {
void trigger_phone_haptics(int) {}
}
