// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// xBRZ through SDL's GPU renderer: a custom fragment shader per backend (SPIR-V, MSL, HLSL).
#include "openlf2/detail/sdl/frame_upscaler.hpp"
#include "openlf2/detail/sdl/xbrz_shader.hpp"
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

// SDL's GPU render-state API needs SDL 3.4.0+; older system SDL3 falls back to gl_upscaler.cpp.
#if SDL_VERSION_ATLEAST(3, 4, 0)

namespace openlf2 {
namespace {
struct ReleaseShader {
    SDL_GPUDevice* device = nullptr;
    void operator()(SDL_GPUShader* shader) const noexcept { SDL_ReleaseGPUShader(device, shader); }
};
struct DestroyState {
    void operator()(SDL_GPURenderState* state) const noexcept { SDL_DestroyGPURenderState(state); }
};
std::string concatenate(const auto& parts) {
    std::string text;
    for (const auto part : parts) text += part;
    return text;
}
#ifdef _WIN32
// Just enough of d3dcompiler_47.dll to turn HLSL into DXBC, without Windows or D3D headers: the
// library is loaded at run time and its ID3DBlob is used through its vtable.
#if defined(_WIN64)
#define OPENLF2_D3D_CALL
#else
#define OPENLF2_D3D_CALL __stdcall
#endif
struct D3DBlob {
    struct Vtable {
        void* query_interface;
        void* add_ref;
        unsigned long(OPENLF2_D3D_CALL* release)(D3DBlob*);
        void*(OPENLF2_D3D_CALL* buffer_pointer)(D3DBlob*);
        std::size_t(OPENLF2_D3D_CALL* buffer_size)(D3DBlob*);
    }* vtable;
};
struct ReleaseBlob {
    void operator()(D3DBlob* blob) const noexcept { blob->vtable->release(blob); }
};
using BlobPointer = std::unique_ptr<D3DBlob, ReleaseBlob>;
std::optional<std::vector<Uint8>> compile_hlsl(const std::string& source) {
    struct UnloadLibrary { void operator()(SDL_SharedObject* library) const noexcept { SDL_UnloadObject(library); } };
    const std::unique_ptr<SDL_SharedObject, UnloadLibrary> library(SDL_LoadObject("d3dcompiler_47.dll"));
    if (!library) return std::nullopt;
    using Compile = long(OPENLF2_D3D_CALL*)(const void*, std::size_t, const char*, const void*, void*, const char*,
                                            const char*, unsigned, unsigned, D3DBlob**, D3DBlob**);
    const auto compile = reinterpret_cast<Compile>(SDL_LoadFunction(library.get(), "D3DCompile"));
    if (compile == nullptr) return std::nullopt;
    D3DBlob* code = nullptr;
    D3DBlob* messages = nullptr;
    constexpr unsigned optimization_level3 = 1u << 15;
    // Shader model 5.1: the register spaces SDL's root signature uses.
    const long result = compile(source.data(), source.size(), "xbrz.hlsl", nullptr, nullptr, "main", "ps_5_1",
                                optimization_level3, 0, &code, &messages);
    const BlobPointer code_owner(code);
    const BlobPointer message_owner(messages);
    if (result < 0 || !code) {
        if (messages) {
            std::cerr << "xBRZ: the HLSL shader did not compile: "
                      << std::string_view(static_cast<const char*>(messages->vtable->buffer_pointer(messages)),
                                          messages->vtable->buffer_size(messages)) << '\n';
        }
        return std::nullopt;
    }
    const auto* bytes = static_cast<const Uint8*>(code->vtable->buffer_pointer(code));
    return std::vector<Uint8>(bytes, bytes + code->vtable->buffer_size(code));
}
#endif
class GpuUpscaler final : public FrameUpscaler {
public:
    GpuUpscaler(std::unique_ptr<SDL_GPUShader, ReleaseShader> shader, std::unique_ptr<SDL_GPURenderState, DestroyState> state)
        : shader_(std::move(shader)), state_(std::move(state)) {}
    bool draw(SDL_Renderer& renderer, SDL_Texture& source) override {
        // The shader's one uniform block: source size, source pixels per output pixel.
        const float params[4]{static_cast<float>(source.w), static_cast<float>(source.h),
                              1.0f / presentation_scale(renderer, source), 0.0f};
        if (!SDL_SetGPURenderStateFragmentUniforms(state_.get(), 0, params, sizeof params)) return false;
        if (!SDL_SetGPURenderState(&renderer, state_.get())) return false;
        const bool drawn = SDL_RenderTexture(&renderer, &source, nullptr, nullptr);
        SDL_SetGPURenderState(&renderer, nullptr);
        return drawn;
    }

private:
    // The state owns nothing of the shader (SDL keeps it alive as long as the state), but the
    // shader must be released before the device goes: members are destroyed in reverse order.
    std::unique_ptr<SDL_GPUShader, ReleaseShader> shader_;
    std::unique_ptr<SDL_GPURenderState, DestroyState> state_;
};
}

std::unique_ptr<FrameUpscaler> make_gpu_upscaler(SDL_Renderer& renderer) {
    auto* device = static_cast<SDL_GPUDevice*>(
        SDL_GetPointerProperty(SDL_GetRendererProperties(&renderer), SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr));
    if (device == nullptr) return nullptr;
    // The form of the shader the device takes; `source` and `bytecode` keep generated code alive for the call.
    const auto formats = SDL_GetGPUShaderFormats(device);
    std::string source;
    std::vector<Uint8> bytecode;
    SDL_GPUShaderCreateInfo info{};
    info.entrypoint = "main";
    info.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
    info.num_samplers = 1;
    info.num_uniform_buffers = 1;
    if ((formats & SDL_GPU_SHADERFORMAT_SPIRV) != 0) {
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.code = reinterpret_cast<const Uint8*>(xbrz_shader::spirv.data());
        info.code_size = xbrz_shader::spirv.size() * sizeof(std::uint32_t);
    } else if ((formats & SDL_GPU_SHADERFORMAT_MSL) != 0) {
        source = concatenate(xbrz_shader::msl);
        info.format = SDL_GPU_SHADERFORMAT_MSL;
        info.entrypoint = "main0";
        info.code = reinterpret_cast<const Uint8*>(source.data());
        info.code_size = source.size();
#ifdef _WIN32
    } else if ((formats & SDL_GPU_SHADERFORMAT_DXBC) != 0) {
        auto compiled = compile_hlsl(concatenate(xbrz_shader::hlsl));
        if (!compiled) return nullptr;
        bytecode = std::move(*compiled);
        info.format = SDL_GPU_SHADERFORMAT_DXBC;
        info.code = bytecode.data();
        info.code_size = bytecode.size();
#endif
    } else {
        return nullptr;
    }
    std::unique_ptr<SDL_GPUShader, ReleaseShader> shader(SDL_CreateGPUShader(device, &info), ReleaseShader{device});
    if (!shader) {
        std::cerr << "xBRZ: the GPU shader was rejected: " << SDL_GetError() << '\n';
        return nullptr;
    }
    SDL_GPURenderStateCreateInfo state_info{};
    state_info.fragment_shader = shader.get();
    std::unique_ptr<SDL_GPURenderState, DestroyState> state(SDL_CreateGPURenderState(&renderer, &state_info));
    if (!state) {
        std::cerr << "xBRZ: the GPU render state failed: " << SDL_GetError() << '\n';
        return nullptr;
    }
    return std::make_unique<GpuUpscaler>(std::move(shader), std::move(state));
}
}

#else

namespace openlf2 {
std::unique_ptr<FrameUpscaler> make_gpu_upscaler(SDL_Renderer&) { return nullptr; }
}

#endif
