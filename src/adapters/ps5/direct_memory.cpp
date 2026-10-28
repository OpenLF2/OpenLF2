// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

// PS5 allocator for C++, SDL and Lua allocations (see direct_memory.hpp). Blocks up to 4 MiB
// come from 32 MiB direct-memory chunks in size classes about 25% apart, with one free list per
// class; larger blocks get their own direct mapping. Every block is 32-byte aligned: the PS5
// compiler's default new alignment is 32 and it emits aligned AVX stores into new objects.
// Global operator new routes here, so this file must not allocate: all tables are fixed-size
// and reports are formatted into stack buffers.
#include "openlf2/detail/ps5/direct_memory.hpp"
#include "openlf2/ports/log.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>

extern "C" {
int sceKernelAllocateMainDirectMemory(std::size_t length, std::size_t alignment, int type, std::int64_t* start);
int sceKernelMapDirectMemory(void** address, std::size_t length, int protection, int flags, std::int64_t start,
                             std::size_t alignment);
int sceKernelMunmap(void* address, std::size_t length);
int sceKernelReleaseDirectMemory(std::int64_t start, std::size_t length);
std::size_t malloc_usable_size(void* address);
int sceKernelAvailableFlexibleMemorySize(std::size_t* size);
}

namespace {
constexpr std::size_t mib = 1024 * 1024;
constexpr std::size_t direct_alignment = 0x20000;
// Write-back (CPU-cached) memory with CPU read/write access. Type 3, which SDL's video driver
// and the native template use for scan-out buffers, is write-combined: CPU reads bypass the
// cache, and a heap there ran the game at about 1 frame per second.
constexpr int direct_memory_type = 0;
constexpr int direct_protection = 0x3;
constexpr std::size_t chunk_size = 32 * mib;
constexpr std::size_t arena_limit = 4 * mib;
// Direct memory is shared with the system, so a runaway allocation must fail inside the title
// instead of starving the console.
constexpr std::size_t direct_budget = 1024 * mib;
constexpr std::uint32_t block_magic = 0x4f4c4632; // "OLF2"
constexpr std::size_t block_alignment = 32;
#ifdef __STDCPP_DEFAULT_NEW_ALIGNMENT__
static_assert(block_alignment >= __STDCPP_DEFAULT_NEW_ALIGNMENT__);
#endif

// 32..256 in steps of 32, then four steps per power of two up to arena_limit; all are
// multiples of block_alignment, so consecutive blocks stay aligned.
constexpr auto class_sizes = [] {
    std::array<std::size_t, 64> sizes{};
    std::size_t count = 0;
    for (std::size_t size = 32; size <= 256; size += 32) sizes[count++] = size;
    for (std::size_t base = 256; base < arena_limit; base *= 2) {
        for (std::size_t step = 1; step <= 4; ++step) sizes[count++] = base + base / 4 * step;
    }
    return sizes;
}();
static_assert(class_sizes.back() == arena_limit);
static_assert(std::ranges::all_of(class_sizes, [](std::size_t size) { return size % block_alignment == 0; }));

struct alignas(block_alignment) BlockHeader {
    std::uint32_t size_class;
    std::uint32_t magic;
};
static_assert(sizeof(BlockHeader) == block_alignment);
struct Chunk {
    std::uint8_t* begin = nullptr;
    std::uint8_t* end = nullptr;
};
struct DirectBlock {
    void* address = nullptr;
    std::size_t length = 0;
    std::int64_t start = 0;
    std::size_t size = 0;
};

// Constant-initialized, so allocations made by static constructors can use them.
constinit std::mutex heap_mutex;
constinit std::array<Chunk, 128> chunks{};
constinit std::size_t chunk_count = 0;
constinit std::uint8_t* bump = nullptr;
constinit std::uint8_t* bump_end = nullptr;
constinit std::array<void*, class_sizes.size()> free_lists{};
constinit std::array<DirectBlock, 256> direct_blocks{};
constinit std::size_t direct_in_use = 0;
// Why direct memory was last refused, for the operator new failure report.
constinit const char* direct_failure = "not tried";
constinit int direct_failure_code = 0;

// Caller holds heap_mutex. Maps `length` bytes of direct memory within direct_budget,
// recording the physical start for release.
void* map_direct(std::size_t length, std::int64_t& start) noexcept {
    if (length > direct_budget - direct_in_use) {
        direct_failure = "1 GiB budget reached";
        direct_failure_code = 0;
        return nullptr;
    }
    if (const int code = sceKernelAllocateMainDirectMemory(length, direct_alignment, direct_memory_type, &start)) {
        direct_failure = "allocate";
        direct_failure_code = code;
        return nullptr;
    }
    void* address = nullptr;
    if (const int code = sceKernelMapDirectMemory(&address, length, direct_protection, 0, start, direct_alignment)) {
        (void)sceKernelReleaseDirectMemory(start, length);
        direct_failure = "map";
        direct_failure_code = code;
        return nullptr;
    }
    direct_in_use += length;
    return address;
}
// Caller holds heap_mutex.
void unmap_direct(const DirectBlock& block) noexcept {
    (void)sceKernelMunmap(block.address, block.length);
    (void)sceKernelReleaseDirectMemory(block.start, block.length);
    direct_in_use -= block.length;
}

std::size_t class_of(std::size_t size) noexcept {
    return static_cast<std::size_t>(std::ranges::lower_bound(class_sizes, size) - class_sizes.begin());
}
// Caller holds heap_mutex.
bool in_arena(const void* address) noexcept {
    const auto* byte = static_cast<const std::uint8_t*>(address);
    for (std::size_t index = 0; index < chunk_count; ++index) {
        if (byte >= chunks[index].begin && byte < chunks[index].end) return true;
    }
    return false;
}
BlockHeader* header_of(void* payload) noexcept {
    return static_cast<BlockHeader*>(payload) - 1;
}
// Caller holds heap_mutex.
void* arena_allocate(std::size_t size) noexcept {
    const std::size_t size_class = class_of(size);
    void* payload = free_lists[size_class];
    if (payload != nullptr) {
        std::memcpy(&free_lists[size_class], payload, sizeof(void*));
        return payload;
    }
    const std::size_t block = sizeof(BlockHeader) + class_sizes[size_class];
    if (bump == nullptr || static_cast<std::size_t>(bump_end - bump) < block) {
        if (chunk_count == chunks.size()) {
            direct_failure = "chunk table full";
            direct_failure_code = 0;
            return nullptr;
        }
        std::int64_t start = 0;
        auto* chunk = static_cast<std::uint8_t*>(map_direct(chunk_size, start));
        if (chunk == nullptr) return nullptr;
        chunks[chunk_count++] = {chunk, chunk + chunk_size};
        bump = chunk;
        bump_end = chunk + chunk_size;
    }
    auto* header = reinterpret_cast<BlockHeader*>(bump);
    bump += block;
    *header = {static_cast<std::uint32_t>(size_class), block_magic};
    return header + 1;
}
// Caller holds heap_mutex.
void arena_free(void* payload) noexcept {
    BlockHeader* header = header_of(payload);
    if (header->magic != block_magic) __builtin_trap();
    std::memcpy(payload, &free_lists[header->size_class], sizeof(void*));
    free_lists[header->size_class] = payload;
}
// Caller holds heap_mutex.
void* direct_allocate(std::size_t size) noexcept {
    const auto slot = std::ranges::find(direct_blocks, nullptr, &DirectBlock::address);
    if (slot == direct_blocks.end()) {
        direct_failure = "large table full";
        direct_failure_code = 0;
        return nullptr;
    }
    const std::size_t length = (size + direct_alignment - 1) / direct_alignment * direct_alignment;
    std::int64_t start = 0;
    void* address = map_direct(length, start);
    if (address != nullptr) *slot = {address, length, start, size};
    return address;
}
// Caller holds heap_mutex. The requested size of a large block, or 0 if `address` is not one.
std::size_t direct_size(const void* address) noexcept {
    const auto block = std::ranges::find(direct_blocks, address, &DirectBlock::address);
    return block == direct_blocks.end() ? 0 : block->size;
}
// Caller holds heap_mutex.
bool direct_free(void* address) noexcept {
    const auto block = std::ranges::find(direct_blocks, address, &DirectBlock::address);
    if (block == direct_blocks.end()) return false;
    unmap_direct(*block);
    *block = {};
    return true;
}
// Caller holds heap_mutex.
void* allocate_locked(std::size_t size) noexcept {
    if (size == 0) size = 1;
    return size <= arena_limit ? arena_allocate(size) : direct_allocate(size);
}
} // namespace

namespace openlf2::ps5 {
void* fallback_malloc(std::size_t size) noexcept {
    const std::lock_guard lock(heap_mutex);
    return allocate_locked(size);
}
void* fallback_calloc(std::size_t count, std::size_t size) noexcept {
    std::size_t total = 0;
    if (__builtin_mul_overflow(count, size, &total)) return nullptr;
    void* address = fallback_malloc(total);
    if (address != nullptr) std::memset(address, 0, total);
    return address;
}
void* fallback_realloc(void* address, std::size_t size) noexcept {
    if (address == nullptr) return fallback_malloc(size);
    std::size_t old_size = 0;
    bool from_libc = false;
    {
        const std::lock_guard lock(heap_mutex);
        if (in_arena(address)) {
            old_size = class_sizes[header_of(address)->size_class];
            if (size <= old_size) return address;
        } else if (const std::size_t large = direct_size(address)) {
            old_size = large;
        } else {
            // Allocated by libc before SDL switched to these functions.
            old_size = malloc_usable_size(address);
            from_libc = true;
        }
    }
    void* moved = fallback_malloc(size);
    if (moved == nullptr) return nullptr;
    std::memcpy(moved, address, std::min(old_size, size));
    if (from_libc) {
        std::free(address);
    } else {
        fallback_free(address);
    }
    return moved;
}
void report_heap_usage(unsigned long long frame) noexcept {
    std::size_t used_chunks = 0;
    std::size_t large_blocks = 0;
    std::size_t large_bytes = 0;
    std::size_t in_use = 0;
    {
        const std::lock_guard lock(heap_mutex);
        used_chunks = chunk_count;
        in_use = direct_in_use;
        for (const auto& block : direct_blocks) {
            if (block.address == nullptr) continue;
            ++large_blocks;
            large_bytes += block.length;
        }
    }
    std::size_t flexible = 0;
    (void)sceKernelAvailableFlexibleMemorySize(&flexible);
    char text[160];
    std::snprintf(text, sizeof(text),
                  "frame %llu: direct %zu MiB (%zu chunks, %zu large blocks %zu MiB), free flex %zu MiB", frame,
                  in_use / mib, used_chunks, large_blocks, large_bytes / mib, flexible / mib);
    openlf2::log_write(openlf2::LogLevel::debug, text);
}
void fallback_free(void* address) noexcept {
    if (address == nullptr) return;
    {
        const std::lock_guard lock(heap_mutex);
        if (in_arena(address)) {
            arena_free(address);
            return;
        }
        if (direct_free(address)) return;
    }
    std::free(address);
}

void* lua_alloc(void*, void* block, std::size_t, std::size_t size) noexcept {
    if (size == 0) {
        fallback_free(block);
        return nullptr;
    }
    return fallback_realloc(block, size);
}
} // namespace openlf2::ps5

// Replacing the unaligned forms routes every C++ allocation in the title through this heap;
// libc++'s nothrow, sized and array forms forward to these. Aligned forms keep libc++'s own
// posix_memalign pairing.
void* operator new(std::size_t size) {
    if (void* address = openlf2::ps5::fallback_malloc(size)) return address;
    std::size_t chunks_used = 0;
    std::size_t large_bytes = 0;
    const char* failure = nullptr;
    int failure_code = 0;
    {
        const std::lock_guard lock(heap_mutex);
        chunks_used = chunk_count;
        for (const auto& block : direct_blocks) large_bytes += block.length;
        failure = direct_failure;
        failure_code = direct_failure_code;
    }
    char text[160];
    std::snprintf(text, sizeof(text), "new %zu bytes refused (%s rc %x); %zu chunks of 32 MiB, %zu MiB large",
                  size, failure, static_cast<unsigned>(failure_code), chunks_used, large_bytes / mib);
    openlf2::log_write(openlf2::LogLevel::info, text);
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* address) noexcept {
    openlf2::ps5::fallback_free(address);
}
void operator delete[](void* address) noexcept {
    openlf2::ps5::fallback_free(address);
}
void operator delete(void* address, std::size_t) noexcept {
    openlf2::ps5::fallback_free(address);
}
void operator delete[](void* address, std::size_t) noexcept {
    openlf2::ps5::fallback_free(address);
}
