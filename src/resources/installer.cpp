// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/resources/installer.hpp"
#include "openlf2/core/binary.hpp"
#include "openlf2/core/files.hpp"
#include "openlf2/ports/digest.hpp"
#include <algorithm>

namespace openlf2 {
Result<void> identify_installer(std::span<const char> bytes) {
    if (bytes.size() != installer_size) {
        return fail(ErrorCode::unsupported, "installer is " + std::to_string(bytes.size()) + " bytes, not the "
                                                + std::to_string(installer_size) + " of LF2 v2.0a");
    }
    auto digest = sha256(bytes);
    if (!digest) return std::unexpected(digest.error());
    if (*digest != "540c485547e234b0d09595f54d067aafd7176092c308aca61868f3cbc03030e1") {
        return fail(ErrorCode::unsupported, "installer does not match supported LF2 v2.0a SHA-256");
    }
    return {};
}
InstallerArchive::InstallerArchive(Bytes bytes, const Decompressor& decompressor)
    : bytes_(std::move(bytes)), decompressor_(decompressor) {}

Result<std::unique_ptr<InstallerArchive>> InstallerArchive::open(
    const std::filesystem::path& path, const Decompressor& decompressor) {
    auto bytes = read_file(path, installer_size);
    if (!bytes) return std::unexpected(bytes.error());
    auto identified = identify_installer(*bytes);
    if (!identified) return std::unexpected(identified.error());
    auto archive = std::make_unique<InstallerArchive>(std::move(*bytes), decompressor);
    auto indexed = archive->index();
    if (!indexed) return std::unexpected(indexed.error());
    return archive;
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
                if (!entries_.emplace(*name, Entry{payload + offset, compressed, decoded}).second) {
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
    auto result = decompressor_.decode(static_cast<Compression>(view.u8(location.offset)),
        view.slice(location.offset + 1, location.compressed_size - 1), location.decoded_size);
    if (!result) result.error().message = *normalized + ": " + result.error().message;
    return result;
}
}
