#pragma once

namespace WuPatch {

enum LogLevel { LOG_INFO = 0, LOG_WARN = 1, LOG_ERROR = 2 };

typedef void (*LogFn)(int level, const char* line);

// With no sink installed, lines are dropped.
void SetLogSink(LogFn fn);

namespace Log {

#if defined(__GNUC__)
#define WUPATCH_PRINTF(fmtIndex, firstArg) __attribute__((format(printf, fmtIndex, firstArg)))
#else
#define WUPATCH_PRINTF(fmtIndex, firstArg)
#endif

void SetTag(const char* tag);   // prepended to every line
void Info(const char* fmt, ...)  WUPATCH_PRINTF(1, 2);
void Warn(const char* fmt, ...)  WUPATCH_PRINTF(1, 2);
void Error(const char* fmt, ...) WUPATCH_PRINTF(1, 2);

#undef WUPATCH_PRINTF

} // namespace Log
} // namespace WuPatch
