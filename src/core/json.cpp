// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#include "openlf2/core/json.hpp"
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <set>

namespace openlf2 {
namespace {
constexpr std::size_t max_input = 1024 * 1024;
constexpr int max_depth = 32;

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}
    Result<ScriptValue> document() {
        auto value = parse_value(0);
        if (!value) return value;
        skip_space();
        if (position_ != text_.size()) return error("unexpected text after the value");
        return value;
    }
private:
    std::string_view text_;
    std::size_t position_ = 0;

    std::unexpected<Error> error(std::string_view message) const {
        return fail(ErrorCode::format, "JSON: " + std::string(message) + " at offset " + std::to_string(position_));
    }
    void skip_space() {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t'
               || text_[position_] == '\n' || text_[position_] == '\r')) {
            ++position_;
        }
    }
    bool literal(std::string_view word) {
        if (text_.substr(position_, word.size()) != word) return false;
        position_ += word.size();
        return true;
    }
    Result<ScriptValue> parse_value(int depth) {
        if (depth > max_depth) return error("nesting too deep");
        skip_space();
        if (position_ == text_.size()) return error("missing value");
        const char next = text_[position_];
        if (next == '{') return parse_object(depth);
        if (next == '[') return parse_array(depth);
        if (next == '"') {
            auto text = parse_string();
            if (!text) return std::unexpected(text.error());
            return ScriptValue{std::move(*text)};
        }
        if (literal("true")) return ScriptValue{true};
        if (literal("false")) return ScriptValue{false};
        if (literal("null")) return ScriptValue{};
        return parse_number();
    }
    Result<ScriptValue> parse_object(int depth) {
        ++position_;
        ScriptTable table;
        std::set<std::string, std::less<>> keys;
        skip_space();
        if (position_ < text_.size() && text_[position_] == '}') { ++position_; return ScriptValue{std::move(table)}; }
        while (true) {
            skip_space();
            if (position_ == text_.size() || text_[position_] != '"') return error("expected an object key");
            auto key = parse_string();
            if (!key) return std::unexpected(key.error());
            if (!keys.insert(*key).second) return error("duplicate key \"" + *key + "\"");
            skip_space();
            if (position_ == text_.size() || text_[position_] != ':') return error("expected ':'");
            ++position_;
            auto value = parse_value(depth + 1);
            if (!value) return value;
            set_field(table, std::move(*key), std::move(*value));
            skip_space();
            if (position_ < text_.size() && text_[position_] == ',') { ++position_; continue; }
            if (position_ < text_.size() && text_[position_] == '}') { ++position_; break; }
            return error("expected ',' or '}'");
        }
        return ScriptValue{std::move(table)};
    }
    Result<ScriptValue> parse_array(int depth) {
        ++position_;
        ScriptTable table;
        skip_space();
        if (position_ < text_.size() && text_[position_] == ']') { ++position_; return ScriptValue{std::move(table)}; }
        std::int32_t index = 1;
        while (true) {
            auto value = parse_value(depth + 1);
            if (!value) return value;
            if (index == std::numeric_limits<std::int32_t>::max()) return error("array too long");
            set_field(table, index++, std::move(*value));
            skip_space();
            if (position_ < text_.size() && text_[position_] == ',') { ++position_; continue; }
            if (position_ < text_.size() && text_[position_] == ']') { ++position_; break; }
            return error("expected ',' or ']'");
        }
        return ScriptValue{std::move(table)};
    }
    Result<unsigned> hex4() {
        if (text_.size() - position_ < 4) return error("short \\u escape");
        unsigned value = 0;
        for (int digit = 0; digit < 4; ++digit) {
            const char c = text_[position_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value |= static_cast<unsigned>(c - 'A' + 10);
            else return error("invalid \\u escape");
        }
        return value;
    }
    static void append_utf8(std::string& out, unsigned code) {
        if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xc0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else if (code < 0x10000) {
            out += static_cast<char>(0xe0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        } else {
            out += static_cast<char>(0xf0 | (code >> 18));
            out += static_cast<char>(0x80 | ((code >> 12) & 0x3f));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3f));
            out += static_cast<char>(0x80 | (code & 0x3f));
        }
    }
    Result<std::string> parse_string() {
        ++position_;
        std::string out;
        while (true) {
            if (position_ == text_.size()) return error("unterminated string");
            const char c = text_[position_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) return error("control character in string");
            if (c != '\\') { out += c; continue; }
            if (position_ == text_.size()) return error("unterminated escape");
            const char escape = text_[position_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                auto code = hex4();
                if (!code) return std::unexpected(code.error());
                unsigned value = *code;
                if (value >= 0xd800 && value <= 0xdbff) {
                    if (!literal("\\u")) return error("unpaired surrogate");
                    auto low = hex4();
                    if (!low) return std::unexpected(low.error());
                    if (*low < 0xdc00 || *low > 0xdfff) return error("unpaired surrogate");
                    value = 0x10000 + ((value - 0xd800) << 10) + (*low - 0xdc00);
                } else if (value >= 0xdc00 && value <= 0xdfff) {
                    return error("unpaired surrogate");
                }
                append_utf8(out, value);
                break;
            }
            default: return error("invalid escape");
            }
        }
    }
    Result<ScriptValue> parse_number() {
        const auto start = position_;
        if (position_ < text_.size() && text_[position_] == '-') ++position_;
        const auto digits = [&] {
            const auto first = position_;
            while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') ++position_;
            return position_ - first;
        };
        const auto integer_start = position_;
        const auto integer_digits = digits();
        if (integer_digits == 0) return error("invalid value");
        if (integer_digits > 1 && text_[integer_start] == '0') return error("leading zero");
        bool fraction = false;
        if (position_ < text_.size() && text_[position_] == '.') {
            ++position_;
            if (digits() == 0) return error("missing fraction digits");
            fraction = true;
        }
        if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E')) {
            ++position_;
            if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-')) ++position_;
            if (digits() == 0) return error("missing exponent digits");
            fraction = true;
        }
        const auto number = text_.substr(start, position_ - start);
        if (!fraction) {
            std::int32_t value = 0;
            const auto [end, code] = std::from_chars(number.data(), number.data() + number.size(), value);
            if (code == std::errc{} && end == number.data() + number.size()) return ScriptValue{value};
        }
        double value = 0.0;
        // Android's libc++ can provide integral charconv without the floating
        // overloads. A classic-locale stream keeps JSON parsing locale independent.
        std::istringstream input{std::string(number)};
        input.imbue(std::locale::classic());
        input >> std::noskipws >> value;
        if (!input || input.rdbuf()->sgetc() != std::char_traits<char>::eof() || !std::isfinite(value)) {
            return error("number out of range");
        }
        return ScriptValue{value};
    }
};

void write_string(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                constexpr char digits[] = "0123456789abcdef";
                out += "\\u00";
                out += digits[(static_cast<unsigned char>(c) >> 4) & 0xf];
                out += digits[static_cast<unsigned char>(c) & 0xf];
            } else {
                out += c;
            }
        }
    }
    out += '"';
}

Result<void> write_value(std::string& out, const ScriptValue& value, int depth) {
    if (depth > max_depth) return fail(ErrorCode::format, "JSON: nesting too deep");
    const auto& content = value.value;
    if (std::holds_alternative<std::monostate>(content)) {
        out += "null";
    } else if (const auto* flag = std::get_if<bool>(&content)) {
        out += *flag ? "true" : "false";
    } else if (const auto* integer = std::get_if<std::int32_t>(&content)) {
        out += std::to_string(*integer);
    } else if (const auto* number = std::get_if<double>(&content)) {
        if (!std::isfinite(*number)) return fail(ErrorCode::format, "JSON: non-finite number");
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(std::numeric_limits<double>::max_digits10) << *number;
        if (!output) return fail(ErrorCode::format, "JSON: number formatting failed");
        out += output.str();
    } else if (const auto* text = std::get_if<std::string>(&content)) {
        write_string(out, *text);
    } else {
        const auto& table = std::get<ScriptTable>(content);
        bool array = true;
        bool object = true;
        for (std::size_t index = 0; index < table.size(); ++index) {
            const auto* position = std::get_if<std::int32_t>(&table[index].key);
            if (position == nullptr || *position != static_cast<std::int32_t>(index + 1)) array = false;
            if (position != nullptr) object = false;
        }
        if (!array && !object) return fail(ErrorCode::format, "JSON: table mixes array and object keys");
        const std::string indent(static_cast<std::size_t>(depth + 1) * 2, ' ');
        out += array ? '[' : '{';
        for (std::size_t index = 0; index < table.size(); ++index) {
            out += index == 0 ? "\n" : ",\n";
            out += indent;
            if (!array) {
                write_string(out, std::get<std::string>(table[index].key));
                out += ": ";
            }
            auto written = write_value(out, table[index].value, depth + 1);
            if (!written) return written;
        }
        if (!table.empty()) {
            out += '\n';
            out.append(static_cast<std::size_t>(depth) * 2, ' ');
        }
        out += array ? ']' : '}';
    }
    return {};
}
}

Result<ScriptValue> parse_json(std::string_view text) {
    if (text.size() > max_input) return fail(ErrorCode::limit, "JSON: input exceeds 1 MiB");
    if (text.starts_with("\xef\xbb\xbf")) text.remove_prefix(3);
    return Parser(text).document();
}

Result<std::string> write_json(const ScriptValue& value) {
    std::string out;
    auto written = write_value(out, value, 0);
    if (!written) return std::unexpected(written.error());
    out += '\n';
    if (out.size() > max_input) return fail(ErrorCode::limit, "JSON: output exceeds 1 MiB");
    return out;
}
}
