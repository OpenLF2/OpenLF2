// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// iOS's Taptic Engine via UIImpactFeedbackGenerator; called directly since SDL already runs the
// main loop on the main thread, which UIKit requires.
//
// Not built or run on a device: this repository's build host has no Apple toolchain installed.
#include "openlf2/detail/sdl/haptics.hpp"
#include <algorithm>
#import <UIKit/UIKit.h>

namespace openlf2 {
void trigger_phone_haptics(int strength) {
    const int clamped = std::clamp(strength, 0, 100);
    // UIImpactFeedbackGenerator has no continuous intensity input before iOS 13's
    // -impactOccurredWithIntensity:; style picks the closest fixed feel below that.
    UIImpactFeedbackStyle style = UIImpactFeedbackStyleLight;
    if (clamped >= 70) style = UIImpactFeedbackStyleHeavy;
    else if (clamped >= 35) style = UIImpactFeedbackStyleMedium;
    UIImpactFeedbackGenerator* generator = [[UIImpactFeedbackGenerator alloc] initWithStyle:style];
    [generator prepare];
    if (@available(iOS 13.0, *)) {
        [generator impactOccurredWithIntensity:std::max(0.1, static_cast<double>(clamped) / 100.0)];
    } else {
        [generator impactOccurred];
    }
}
}
