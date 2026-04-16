#include <ezgl/logutils.hpp>

namespace ezgl {

void log_message_v(const char* level, const char* file, int line, const char* fmt, va_list ap)
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    char time_buf[32];
    std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm);

    std::fprintf(stderr, "%s %s: %s:%d: ", time_buf, level, file, line);
    std::vfprintf(stderr, fmt, ap);
    std::fputc('\n', stderr);
}

void log_message(const char* level, const char* file, int line, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    log_message_v(level, file, line, fmt, ap);
    va_end(ap);
}

} // namespace ezgl
