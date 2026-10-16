// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/resources/installer.hpp"
#include "openlf2/core/binary.hpp"
#include "openlf2/core/files.hpp"
#include "openlf2/ports/digest.hpp"
#include <algorithm>
#include <optional>

namespace openlf2 {
namespace {
std::size_t overlay_offset(const BinaryView& file) {
    if (file.text(0, 2) != "MZ") throw FormatError("missing DOS header");
    const std::size_t pe = file.u32(0x3c);
    if (file.text(pe, 4) != std::string("PE\0\0", 4)) throw FormatError("missing PE header");
    const std::size_t sections = pe + 24 + file.u16(pe + 20);
    const auto count = file.u16(pe + 6);
    file.slice(sections, std::size_t{count} * 40);
    std::size_t overlay = 0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto section = sections + index * 40;
        const std::size_t offset = file.u32(section + 20), size = file.u32(section + 16);
        file.slice(offset, size);
        overlay = std::max(overlay, offset + size);
    }
    return overlay;
}
struct NsisHeader {
    Bytes bytes;
    std::size_t data_offset;
    std::size_t instructions;
    std::size_t count;
    std::size_t strings;
    std::size_t strings_size;
};
NsisHeader nsis_header(std::span<const char> bytes, const Decompressor& decompressor) {
    const BinaryView file(bytes);
    const auto overlay = overlay_offset(file);
    // These two pinned packages use a customized NSIS 2 header and opcodes.
    constexpr std::array<std::uint32_t, 4> signature{0x5ac8fd16, 0x0fa48c1d, 0xac86f46d, 0x3c8cf36f};
    for (std::size_t i = 0; i < signature.size(); ++i) {
        if (file.u32(overlay + i * 4) != signature[i]) throw FormatError("missing LF2 NSIS signature");
    }
    if (file.u32(overlay + 20) != bytes.size() - overlay || file.u32(overlay + 24) != 0) {
        throw FormatError("invalid LF2 NSIS package bounds");
    }
    const auto compressed = file.u32(overlay + 28);
    if (!(compressed & 0x80000000u)) throw FormatError("uncompressed LF2 NSIS header");
    const std::size_t size = compressed & 0x7fffffffu;
    auto decoded = decompressor.decode(Compression::nsis_deflate, file.slice(overlay + 32, size), file.u32(overlay + 16));
    if (!decoded) throw FormatError(decoded.error().message);
    const BinaryView header(*decoded);
    const std::size_t count = header.u32(40), instructions = header.u32(44);
    const std::size_t strings = header.u32(60), strings_end = header.u32(52);
    if (count > 4096 || strings_end < strings) throw FormatError("invalid LF2 NSIS header blocks");
    header.slice(instructions, count * 28);
    header.slice(strings, strings_end - strings);
    return {std::move(*decoded), overlay + 32 + size, instructions, count, strings, strings_end - strings};
}
std::string nsis_string(const NsisHeader& header, std::size_t offset) {
    if (offset >= header.strings_size) throw FormatError("LF2 NSIS string outside table");
    const auto bytes = BinaryView(header.bytes).slice(header.strings + offset, header.strings_size - offset);
    const auto end = std::ranges::find(bytes, '\0');
    if (end == bytes.end()) throw FormatError("unterminated LF2 NSIS string");
    return {bytes.begin(), end};
}
struct NsisBlock { std::size_t offset; std::size_t size; Compression method; };
NsisBlock nsis_block(std::span<const char> bytes, const NsisHeader& header, std::size_t offset) {
    const BinaryView file(bytes);
    if (offset > bytes.size() - header.data_offset) throw FormatError("LF2 NSIS file outside payload");
    const auto position = header.data_offset + offset;
    const auto flags = file.u32(position);
    const std::size_t size = flags & 0x7fffffffu;
    file.slice(position + 4, size);
    return {position + 4, size, flags & 0x80000000u ? Compression::nsis_deflate : Compression::stored};
}
}
Result<void> identify_installer(std::span<const char> bytes) {
    if (bytes.size() != installer_size && bytes.size() != alternate_installer_size) {
        return fail(ErrorCode::unsupported, "installer is " + std::to_string(bytes.size()) +
            " bytes; supported LF2 v2.0a installers are " + std::to_string(installer_size) +
            " or " + std::to_string(alternate_installer_size) + " bytes");
    }
    auto digest = sha256(bytes);
    if (!digest) return std::unexpected(digest.error());
    const auto expected = bytes.size() == installer_size
        ? "540c485547e234b0d09595f54d067aafd7176092c308aca61868f3cbc03030e1"
        : "2d9629fa1ca67d636c595842aafeb563603b97de4c8530d0bcb7641ad717e9ee";
    if (*digest != expected) {
        return fail(ErrorCode::unsupported, "installer does not match supported LF2 v2.0a SHA-256");
    }
    return {};
}
InstallerArchive::InstallerArchive(Bytes bytes, const Decompressor& decompressor)
    : bytes_(std::move(bytes)), decompressor_(decompressor) {}

Result<std::unique_ptr<InstallerArchive>> InstallerArchive::open(
    const std::filesystem::path& path, const Decompressor& decompressor) {
    auto bytes = read_file(path, installer_max_size);
    if (!bytes) return std::unexpected(bytes.error());
    auto identified = identify_installer(*bytes);
    if (!identified) return std::unexpected(identified.error());
    auto archive = std::make_unique<InstallerArchive>(std::move(*bytes), decompressor);
    if (archive->bytes_.size() == alternate_installer_size) {
        auto unwrapped = archive->unwrap_nsis();
        if (!unwrapped) return std::unexpected(unwrapped.error());
        auto indexed = archive->index_nsis();
        if (!indexed) return std::unexpected(indexed.error());
        return archive;
    }
    auto indexed = archive->index();
    if (!indexed) return std::unexpected(indexed.error());
    return archive;
}

Result<void> InstallerArchive::unwrap_nsis() {
    try {
        const auto header = nsis_header(bytes_, decompressor_);
        const BinaryView commands(header.bytes);
        std::optional<Bytes> host;
        for (std::size_t i = 0; i < header.count; ++i) {
            const auto command = header.instructions + i * 28;
            if (commands.u32(command) != 36 ||
                nsis_string(header, commands.u32(command + 8)) != "\xfd\x9a\x80\\hostsetup.exe") continue;
            if (host) throw FormatError("duplicate LF2 NSIS host package");
            const auto block = nsis_block(bytes_, header, commands.u32(command + 12));
            auto decoded = decompressor_.decode(block.method, BinaryView(bytes_).slice(block.offset, block.size), 30'799'229);
            if (!decoded) return std::unexpected(decoded.error());
            host = std::move(*decoded);
        }
        if (!host) throw FormatError("missing LF2 NSIS host package");
        auto digest = sha256(*host);
        if (!digest) return std::unexpected(digest.error());
        if (*digest != "a3e8de6e7a17f40956a2b7dcee7cd53a5c17fbd0c41a7bf68b87ff199a185e08") {
            throw FormatError("LF2 NSIS host package SHA-256 mismatch");
        }
        bytes_ = std::move(*host);
        return {};
    } catch (const FormatError& error) { return fail(ErrorCode::format, error.what()); }
}

Result<void> InstallerArchive::index_nsis() {
    try {
        const auto header = nsis_header(bytes_, decompressor_);
        const BinaryView commands(header.bytes);
        std::optional<std::string> directory;
        for (std::size_t i = 0; i < header.count; ++i) {
            const auto command = header.instructions + i * 28;
            const auto opcode = commands.u32(command);
            // Read only file declarations under $INSTDIR. No installer commands,
            // plugins, toolbar files, registry operations or executables are run.
            if (opcode == 34 && commands.u32(command + 8) == 1) {
                auto path = nsis_string(header, commands.u32(command + 4));
                directory.reset();
                if (path.starts_with("\xfd\x95\x80")) {
                    path.erase(0, 3);
                    if (path.starts_with('\\')) path.erase(0, 1);
                    if (path.empty()) directory = "";
                    else {
                        auto normalized = virtual_path(path);
                        if (!normalized) return std::unexpected(normalized.error());
                        directory = *normalized + "/";
                    }
                }
            } else if (opcode == 36 && directory) {
                const auto filename = nsis_string(header, commands.u32(command + 8));
                if (filename.starts_with('\xfd')) continue;
                auto path = virtual_path(*directory + filename);
                if (!path) return std::unexpected(path.error());
                const auto block = nsis_block(bytes_, header, commands.u32(command + 12));
                const auto decoded = block.method == Compression::stored ? block.size : 0;
                if (!entries_.emplace(*path, Entry{block.offset, block.size, decoded, block.method}).second) {
                    throw FormatError("duplicate LF2 NSIS filename: " + *path);
                }
            }
        }
        if (!entries_.contains("lf2.exe") || !entries_.contains("data/data.txt")) throw FormatError("incomplete LF2 NSIS game files");
        return {};
    } catch (const FormatError& error) { return fail(ErrorCode::format, error.what()); }
}

Result<void> InstallerArchive::index() {
    try {
        const BinaryView file(bytes_);
        if (file.text(0, 2) != "MZ") throw FormatError("missing DOS header");
        const std::size_t pe = file.u32(0x3c);
        file.slice(pe, 24);
        file.slice(pe, 24 + std::size_t{file.u16(pe + 20)});
        if (file.text(pe, 4) != std::string("PE\0\0", 4)) throw FormatError("missing PE header");
        const auto section_count = file.u16(pe + 6);
        const std::size_t sections = pe + 24 + file.u16(pe + 20);
        file.slice(sections, std::size_t{section_count} * 40);
        std::size_t overlay = 0;
        for (std::size_t index = 0; index < section_count; ++index) {
            const auto section = sections + index * 40;
            const std::size_t offset = file.u32(section + 20);
            const std::size_t size = file.u32(section + 16);
            file.slice(offset, size);
            overlay = std::max(overlay, offset + size);
        }
        if (file.text(overlay, 6) != "wwgT)H") throw FormatError("missing installer signature");
        std::size_t cursor = overlay + 6;
        std::size_t payload = 0;
        std::size_t payload_size = 0;
        Bytes table;
        while (cursor < bytes_.size()) {
            const auto id = file.u16(cursor);
            const auto flags = file.u16(cursor + 2);
            const std::size_t size = file.u32(cursor + 4);
            const auto body = cursor + 8;
            if (id == 0x7f7f) {
                if (flags != 0 || file.u32(body) != size) throw FormatError("invalid payload header");
                payload = body + 4;
                payload_size = size;
                file.slice(payload, size);
                if (payload + size != bytes_.size()) throw FormatError("payload does not end at EOF");
                break;
            }
            file.slice(body, size);
            if (flags != 1 || size < 5) throw FormatError("unsupported metadata record");
            if (id == 0x143a) {
                if (!table.empty()) throw FormatError("duplicate file table");
                auto decoded = decompressor_.decode(static_cast<Compression>(file.u8(body + 4)),
                    file.slice(body + 5, size - 5), file.u32(body));
                if (!decoded) return std::unexpected(decoded.error());
                table = std::move(*decoded);
            }
            cursor = body + size;
        }
        if (payload == 0 || table.empty()) throw FormatError("missing payload or file table");
        const BinaryView records(table);
        const auto count = records.u32(0);
        if (count > 4096) throw FormatError("excessive file-table count");
        cursor = 4;
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::size_t size = records.u16(cursor);
            if (size < 4) throw FormatError("short file-table record");
            const BinaryView record(records.slice(cursor, size));
            const auto kind = record.u16(2);
            if (kind == 0) {
                if (size < 63) throw FormatError("short file record");
                const auto name_bytes = record.slice(62, size - 62);
                const auto terminator = std::ranges::find(name_bytes, '\0');
                if (terminator == name_bytes.end()) throw FormatError("unterminated filename");
                auto name = virtual_path(std::string(name_bytes.begin(), terminator));
                if (!name) return std::unexpected(name.error());
                const std::size_t offset = record.u32(6);
                const std::size_t compressed = record.u32(10);
                const std::size_t decoded = record.u32(18);
                if (compressed < 1 || offset > payload_size || compressed > payload_size - offset ||
                    decoded > 64 * 1024 * 1024) throw FormatError("invalid indexed file bounds");
                const auto method = static_cast<Compression>(file.u8(payload + offset));
                if (method != Compression::stored && method != Compression::zlib && method != Compression::bzip2) {
                    throw FormatError("unsupported installer compression method");
                }
                if (!entries_.emplace(*name, Entry{payload + offset + 1, compressed - 1, decoded, method}).second) {
                    throw FormatError("duplicate normalized filename: " + *name);
                }
            } else if (kind != 2 || index != 0) throw FormatError("unsupported file-table record kind");
            cursor += size;
        }
        if (cursor != table.size()) throw FormatError("trailing file-table bytes");
        return {};
    } catch (const FormatError& error) { return fail(ErrorCode::format, error.what()); }
}

Result<Bytes> InstallerArchive::read(std::string_view path) const {
    auto normalized = virtual_path(path);
    if (!normalized) return std::unexpected(normalized.error());
    const auto entry = entries_.find(*normalized);
    if (entry == entries_.end()) return fail(ErrorCode::io, "installer resource missing: " + *normalized);
    const auto& location = entry->second;
    const BinaryView view(bytes_);
    auto result = decompressor_.decode(location.method,
        view.slice(location.offset, location.compressed_size), location.decoded_size);
    if (!result) result.error().message = *normalized + ": " + result.error().message;
    return result;
}
}
