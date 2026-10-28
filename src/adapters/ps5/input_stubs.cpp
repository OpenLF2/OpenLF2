// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// PacBrew's SDL2 video driver opens a USB keyboard and polls the IME dialog unconditionally.
// A shell-launched title does not get libSceKeyboard or libSceImeDialog bound without loading
// their system modules first, so those imports resolve to null and SDL_Init crashes. OpenLF2
// is controller-driven on PS5: report both services as unavailable so SDL skips them.
namespace {
constexpr int unavailable = static_cast<int>(0x80020016); // generic "not supported" error
constexpr int ime_dialog_status_none = 0;
} // namespace

extern "C" {
int sceKeyboardInit() {
    return unavailable;
}

int sceKeyboardOpen(int, int, int, void*) {
    return unavailable;
}

int sceKeyboardReadState(int, void*) {
    return unavailable;
}

int sceKeyboardClose(int) {
    return 0;
}

int sceImeDialogInit(const void*, void*) {
    return unavailable;
}

int sceImeDialogGetStatus() {
    return ime_dialog_status_none;
}

int sceImeDialogGetResult(void*) {
    return unavailable;
}

int sceImeDialogTerm() {
    return 0;
}
} // extern "C"
