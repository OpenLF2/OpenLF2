// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// Android's vibration motor via Vibrator.vibrate() through JNI. Requires
// android.permission.VIBRATE. The JNI thread stays attached for the app's lifetime, so local
// references need explicit `Local` wrappers rather than a Java-side return boundary to free them.
//
// Not built or run on a device: this repository's build host has no Android NDK installed.
#include "openlf2/detail/sdl/haptics.hpp"
#include <algorithm>
#include <jni.h>
#include <SDL3/SDL.h>

namespace openlf2 {
namespace {
constexpr int fixed_duration_ms = 120;

template <typename T> class Local {
public:
    Local(JNIEnv& env, T value) : env_(env), value_(value) {}
    Local(const Local&) = delete;
    Local& operator=(const Local&) = delete;
    ~Local() { if (value_ != nullptr) env_.DeleteLocalRef(value_); }
    T get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

private:
    JNIEnv& env_;
    T value_;
};

// Clears a pending JNI exception and reports failure, so one missing class/method (an unexpected
// OEM Android build) silently skips the rumble instead of crashing the game.
bool failed(JNIEnv& env) {
    if (!env.ExceptionCheck()) return false;
    env.ExceptionClear();
    return true;
}
}

void trigger_phone_haptics(int strength) {
    auto* env_ptr = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    auto* activity_ptr = static_cast<jobject>(SDL_GetAndroidActivity());
    if (env_ptr == nullptr || activity_ptr == nullptr) return;
    JNIEnv& env = *env_ptr;
    const Local activity(env, activity_ptr);
    const int clamped = std::clamp(strength, 0, 100);

    const Local context_class(env, env.FindClass("android/content/Context"));
    const Local activity_class(env, env.GetObjectClass(activity.get()));
    if (!context_class || !activity_class || failed(env)) return;
    const jfieldID vibrator_service_field =
        env.GetStaticFieldID(context_class.get(), "VIBRATOR_SERVICE", "Ljava/lang/String;");
    const jmethodID get_system_service =
        env.GetMethodID(activity_class.get(), "getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;");
    if (vibrator_service_field == nullptr || get_system_service == nullptr || failed(env)) return;
    const Local vibrator_service_name(env, env.GetStaticObjectField(context_class.get(), vibrator_service_field));
    if (!vibrator_service_name || failed(env)) return;
    const Local<jobject> vibrator(env,
        env.CallObjectMethod(activity.get(), get_system_service, vibrator_service_name.get()));
    if (!vibrator || failed(env)) return;
    const Local vibrator_class(env, env.GetObjectClass(vibrator.get()));
    if (!vibrator_class || failed(env)) return;

    // VibrationEffect (API 26+) carries an amplitude; older devices only get an on/off duration.
    if (SDL_GetAndroidSDKVersion() >= 26) {
        const Local effect_class(env, env.FindClass("android/os/VibrationEffect"));
        if (!effect_class || failed(env)) return;
        const jmethodID create_one_shot =
            env.GetStaticMethodID(effect_class.get(), "createOneShot", "(JI)Landroid/os/VibrationEffect;");
        const jmethodID vibrate_effect =
            env.GetMethodID(vibrator_class.get(), "vibrate", "(Landroid/os/VibrationEffect;)V");
        if (create_one_shot == nullptr || vibrate_effect == nullptr || failed(env)) return;
        const jint amplitude = std::clamp(clamped * 255 / 100, 1, 255);
        const Local<jobject> effect(env, env.CallStaticObjectMethod(effect_class.get(), create_one_shot,
            static_cast<jlong>(fixed_duration_ms), amplitude));
        if (!effect || failed(env)) return;
        env.CallVoidMethod(vibrator.get(), vibrate_effect, effect.get());
        failed(env); // nothing more to do either way; only clears a pending exception from the call
    } else {
        const jmethodID vibrate_duration = env.GetMethodID(vibrator_class.get(), "vibrate", "(J)V");
        if (vibrate_duration == nullptr || failed(env)) return;
        env.CallVoidMethod(vibrator.get(), vibrate_duration, static_cast<jlong>(fixed_duration_ms));
        failed(env);
    }
}
}
