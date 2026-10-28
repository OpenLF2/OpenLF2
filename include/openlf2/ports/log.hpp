// SPDX-License-Identifier: MIT
// Copyright (c) 2026 OpenLF2 contributors

#pragma once

#include <sstream>
#include <string_view>

namespace openlf2 {
// debug: frequent status lines. info: startup milestones and other notable events.
// warning and error: something went wrong.
enum class LogLevel { debug, info, warning, error };
// The platform's log sink, called with each finished line. The default (src/core/log.cpp)
// prints info and above to stderr. A platform without a console provides its own definition:
// PS5 appends every line to a file and also shows info and above as a system notification.
void log_write(LogLevel level, std::string_view text) noexcept;
// Stream-style logging: log(LogLevel::warning) << "Music " << name << " stopped";
// The line is written when the temporary ends.
class LogStream {
public:
    explicit LogStream(LogLevel level) noexcept : level_(level) {}
    LogStream(const LogStream&) = delete;
    LogStream& operator=(const LogStream&) = delete;
    ~LogStream() { log_write(level_, stream_.str()); }
    template <class T> LogStream& operator<<(const T& value) {
        stream_ << value;
        return *this;
    }
private:
    LogLevel level_;
    std::ostringstream stream_;
};
inline LogStream log(LogLevel level) noexcept { return LogStream(level); }
} // namespace openlf2
