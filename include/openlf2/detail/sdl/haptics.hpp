// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once
// Private to the SDL adapter: device vibration motor (RumbleCommand::Target::phone). SDL has no
// cross-platform API for this, so each platform has its own implementation or a no-op stub.

namespace openlf2 {
// `strength` is 0..100, already clamped. Best-effort: never throws or reports an error.
void trigger_phone_haptics(int strength);
}
