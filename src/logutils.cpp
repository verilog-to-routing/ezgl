#include <ezgl/logutils.hpp>

#include <cstring>

namespace ezgl {

void log_message_v(const char* level, const char* file, int line, const char* fmt, va_list ap)
{
#ifdef NDEBUG
    // DEBUG messages are diagnostic noise in release; suppress them.
    // Other levels (INFO/WARNING/ERROR/CRITICAL) always pass through.
    if (level && std::strcmp(level, "DEBUG") == 0)
        return;
#endif

    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm);

    // Flush stdout first so that any partially-written application output
    // line is committed before this message lands on stderr. Without this
    // flush, both streams race when a test runner merges them into one
    // file, which corrupts application output lines mid-character.
    std::fflush(stdout);
    std::fprintf(stderr, "%s %s: %s:%d: ", time_buf, level, file, line);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void log_message(const char* level, const char* file, int line, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_message_v(level, file, line, fmt, ap);
    va_end(ap);
}

} // namespace ezgl
