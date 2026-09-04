#include "libwupatch/wupatch_log.h"

#include <stdarg.h>
#include <stdio.h>

namespace WuPatch {

static LogFn       s_sink = 0;
static const char* s_tag = "[wupatch]";

void SetLogSink(LogFn fn) { s_sink = fn; }

namespace Log {

void SetTag(const char* tag)
{
    if (tag && tag[0])
        s_tag = tag;
}

static void emit(int level, const char* fmt, va_list args)
{
    if (!s_sink)
        return;
    char line[192];
    const int used = snprintf(line, sizeof(line), "%s ", s_tag);
    if (used < 0 || used >= (int)sizeof(line))
        return;
    vsnprintf(line + used, sizeof(line) - (size_t)used, fmt, args);
    s_sink(level, line);
}

void Info(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LOG_INFO, fmt, args);
    va_end(args);
}

void Warn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LOG_WARN, fmt, args);
    va_end(args);
}

void Error(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LOG_ERROR, fmt, args);
    va_end(args);
}

} // namespace Log
} // namespace WuPatch
