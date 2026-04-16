#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <source_location>

// These two must remain macros:
//   - they use #expr to stringify the condition (only macros can do that)
//   - they inject 'return' into the *caller's* scope (functions cannot return on behalf of a caller)
#define return_val_if_fail(expr, val)      \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return (val);                        \
  }                                          \
} while (0)

#define return_if_fail(expr)               \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        std::exit(1);                        \
        return;                              \
  }                                          \
} while (0)

namespace ezgl {

// Returns the filename portion of a full path.
constexpr const char* __filename_helper(const char* path)
{
    const char* file = path;
    for (const char* p = path; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            file = p + 1;
        }
    }
    return file;
}

// Core log functions.
// Output format: "YYYY-MM-DD HH:MM:SS LEVEL: file:line: message\n"
// External parsers rely on this format — do not change it.
void log_message(const char* level, const char* file, int line, const char* fmt, ...);
void log_message_v(const char* level, const char* file, int line, const char* fmt, va_list ap);

namespace detail {

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
    log_message_v("INFO", __filename_helper(f.loc.file_name()),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_warning(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("WARNING", __filename_helper(f.loc.file_name()),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_error(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("ERROR", __filename_helper(f.loc.file_name()),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

inline void q_debug(detail::log_fmt f, ...) {
    va_list ap;
    va_start(ap, f);
    log_message_v("DEBUG", __filename_helper(f.loc.file_name()),
                  static_cast<int>(f.loc.line()), f.str, ap);
    va_end(ap);
}

} // namespace ezgl
