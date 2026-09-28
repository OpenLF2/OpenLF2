// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/ports/digest.hpp"
#include <array>
#include <bit>
#include <cstdint>
#include <string>

namespace openlf2 {
namespace {
constexpr std::array<std::uint32_t, 64> rounds{
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void process(std::array<std::uint32_t, 8>& state, const std::array<std::uint8_t, 64>& block) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t index = 0; index < 16; ++index) {
        const auto offset = index * 4;
        words[index] = (static_cast<std::uint32_t>(block[offset]) << 24) |
                       (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
                       (static_cast<std::uint32_t>(block[offset + 2]) << 8) | block[offset + 3];
    }
    for (std::size_t index = 16; index < words.size(); ++index) {
        const auto x = words[index - 15];
        const auto y = words[index - 2];
        words[index] = words[index - 16] + (std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3)) +
                       words[index - 7] + (std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10));
    }
    auto a = state[0], b = state[1], c = state[2], d = state[3];
    auto e = state[4], f = state[5], g = state[6], h = state[7];
    for (std::size_t index = 0; index < words.size(); ++index) {
        const auto s1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
        const auto t1 = h + s1 + ((e & f) ^ (~e & g)) + rounds[index] + words[index];
        const auto s0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
        const auto t2 = s0 + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }
    const std::array updated{a, b, c, d, e, f, g, h};
    for (std::size_t index = 0; index < state.size(); ++index) state[index] += updated[index];
}
}

Result<std::string> sha256(std::span<const char> input) {
    std::array<std::uint32_t, 8> state{0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::array<std::uint8_t, 64> block{};
    std::size_t used = 0;
    for (const char value : input) {
        block[used++] = static_cast<std::uint8_t>(value);
        if (used == block.size()) {
            process(state, block);
            block.fill(0);
            used = 0;
        }
    }
    block[used++] = 0x80;
    if (used > 56) {
        process(state, block);
        block.fill(0);
    }
    const auto bits = static_cast<std::uint64_t>(input.size()) * 8;
    for (std::size_t index = 0; index < 8; ++index) {
        block[63 - index] = static_cast<std::uint8_t>(bits >> (index * 8));
    }
    process(state, block);
    constexpr char digits[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (const auto word : state) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const auto byte = static_cast<std::uint8_t>(word >> shift);
            hex.push_back(digits[byte >> 4]);
            hex.push_back(digits[byte & 15]);
        }
    }
    return hex;
}
}
