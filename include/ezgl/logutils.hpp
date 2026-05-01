#pragma once

#include <cstdarg>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

// These two must remain macros:
//   - they use #expr to stringify the condition (only macros can do that)
//   - they inject 'return' into the *caller's* scope (functions cannot return on behalf of a caller)
#define return_val_if_fail(label, expr, val)      \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '"           \
        << label << " "                      \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return (val);                        \
  }                                          \
} while (0)

#define return_if_fail(label, expr)          \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '"           \
        << label << " "                      \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return;                              \
  }                                          \
} while (0)

namespace ezgl {

// Core log functions.
// Output format: "YYYY-MM-DD HH:MM:SS LEVEL: file:line: message\n"
// External parsers rely on this format — do not change it.
void log_message(const char* level, const char* file, int line, const char* fmt, ...);
void log_message_v(const char* level, const char* file, int line, const char* fmt, va_list ap);

namespace detail {

// Returns the basename portion of a compiler-provided file path.
// source_location::file_name() often returns a full or relative path; this
// strips everything up to the last '/' or '\' so the log always shows
// "file.cpp:42" rather than "/full/path/to/file.cpp:42".
constexpr std::string_view filename(std::string_view path) noexcept
{
    const auto pos = path.find_last_of("/\\");
    return (pos == std::string_view::npos) ? path : path.substr(pos + 1);
}

// Implicitly constructed from a string literal at the call site.
// std::source_location::current() is evaluated at the *construction site* (i.e. the caller),
// so file/line always point to the q_* call, not to the inline function body.
struct log_fmt {
    const char* str;
    std::source_location loc;

    // Intentionally implicit — the caller writes q_info("fmt", ...) with no extra syntax.
    log_fmt(const char* str,  // NOLINT(google-explicit-constructor)
            std::source_location loc = std::source_location::current()) noexcept
        : str(str), loc(loc) {}
};

} // namespace detail

inline void q_info(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("INFO", detail::filename(f.loc.file_name()).data(),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_warning(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("WARNING", detail::filename(f.loc.file_name()).data(),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_error(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("ERROR", detail::filename(f.loc.file_name()).data(),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_debug(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("DEBUG", detail::filename(f.loc.file_name()).data(),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// Stream-based logging — use when building the message with << is cleaner
// than a printf format string.
//
// Usage:
//   q_debug_stream() << "value=" << x << " other=" << y;
//
// The message is flushed (via log_message) when the temporary is destroyed
// at the end of the full expression.
// ---------------------------------------------------------------------------
class log_stream {
public:
    log_stream(const char* level, const char* file, int line)
        : level_(level), file_(file), line_(line) {}

    log_stream(const log_stream&) = delete;
    log_stream& operator=(const log_stream&) = delete;

    log_stream(log_stream&& other) noexcept
        : level_(other.level_), file_(other.file_), line_(other.line_),
          oss_(std::move(other.oss_)), moved_(false)
    {
        other.moved_ = true;
    }

    ~log_stream() {
        if (!moved_)
            log_message(level_, file_, line_, "%s", oss_.str().c_str());
    }

    template<typename T>
    log_stream& operator<<(const T& v) { oss_ << v; return *this; }

    // Support stream manipulators: std::fixed, std::setprecision, etc.
    log_stream& operator<<(std::ostream& (*manip)(std::ostream&)) {
        manip(oss_);
        return *this;
    }

private:
    const char*        level_;
    const char*        file_;
    int                line_;
    std::ostringstream oss_;
    bool               moved_ = false;
};

inline log_stream q_info_stream(
    std::source_location loc = std::source_location::current()) {
    return log_stream("INFO", detail::filename(loc.file_name()).data(),
                      static_cast<int>(loc.line()));
}

inline log_stream q_warning_stream(
    std::source_location loc = std::source_location::current()) {
    return log_stream("WARNING", detail::filename(loc.file_name()).data(),
                      static_cast<int>(loc.line()));
}

inline log_stream q_error_stream(
    std::source_location loc = std::source_location::current()) {
    return log_stream("ERROR", detail::filename(loc.file_name()).data(),
                      static_cast<int>(loc.line()));
}

inline log_stream q_debug_stream(
    std::source_location loc = std::source_location::current()) {
    return log_stream("DEBUG", detail::filename(loc.file_name()).data(),
                      static_cast<int>(loc.line()));
}

class scope_timer {
public:
    enum class log_level {
        none,
        debug,
        info,
        warning,
        error,
    };

    enum class time_unit {
        automatic,
        milliseconds,
        seconds,
        minutes,
    };

    scope_timer() = default;

    explicit scope_timer(std::string label,
                         log_level level = log_level::debug,
                         time_unit unit = time_unit::automatic,
                         std::source_location loc = std::source_location::current())
        : label_(std::move(label))
        , level_(level)
        , unit_(unit)
        , loc_(loc)
    {}

    scope_timer(const scope_timer&) = delete;
    scope_timer& operator=(const scope_timer&) = delete;
    scope_timer(scope_timer&&) = delete;
    scope_timer& operator=(scope_timer&&) = delete;

    ~scope_timer() noexcept
    {
        if (level_ == log_level::none || label_.empty())
            return;

        try {
            const std::string elapsed = elapsed_string(unit_);
            log_message(level_name(level_),
                        detail::filename(loc_.file_name()).data(),
                        static_cast<int>(loc_.line()),
                        "%s took %s",
                        label_.c_str(),
                        elapsed.c_str());
        } catch (...) {
            // Destructors must not throw while unwinding another exception.
        }
    }

    double elapsed_ms() const noexcept
    {
        return std::chrono::duration<double, std::milli>(
            clock::now() - start_).count();
    }

    double elapsed_sec() const noexcept
    {
        return std::chrono::duration<double>(
            clock::now() - start_).count();
    }

    double elapsed_min() const noexcept
    {
        return elapsed_sec() / 60.0;
    }

    std::string elapsed_string(time_unit unit = time_unit::automatic) const
    {
        const double ms = elapsed_ms();
        if (unit == time_unit::automatic) {
            if (ms < 1000.0)
                unit = time_unit::milliseconds;
            else if (ms < 60000.0)
                unit = time_unit::seconds;
            else
                unit = time_unit::minutes;
        }

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        switch (unit) {
            case time_unit::milliseconds:
                oss << ms << " ms";
                break;
            case time_unit::seconds:
                oss << (ms / 1000.0) << " sec";
                break;
            case time_unit::minutes:
                oss << (ms / 60000.0) << " min";
                break;
            case time_unit::automatic:
                break;
        }
        return oss.str();
    }

private:
    using clock = std::chrono::steady_clock;

    static constexpr const char* level_name(log_level level) noexcept
    {
        switch (level) {
            case log_level::debug:   return "DEBUG";
            case log_level::info:    return "INFO";
            case log_level::warning: return "WARNING";
            case log_level::error:   return "ERROR";
            case log_level::none:    return "DEBUG";
        }
        return "DEBUG";
    }

    clock::time_point   start_ = clock::now();
    std::string         label_;
    log_level           level_ = log_level::none;
    time_unit           unit_ = time_unit::automatic;
    std::source_location loc_ = std::source_location::current();
};

} // namespace ezgl
