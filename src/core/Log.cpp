#include "core/Log.h"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>

namespace mm {
namespace {

const char* LevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Info:  return "[info] ";
        case LogLevel::Warn:  return "[warn] ";
        case LogLevel::Error: return "[error] ";
    }
    return "[?] ";
}

void WriteLine(const char* tag, const char* body) {
    char line[2048];
    std::snprintf(line, sizeof(line), "%s%s\n", tag, body);
    ::OutputDebugStringA(line);
    std::fputs(line, stderr);
}

}  // namespace

void LogMessage(LogLevel level, const char* fmt, ...) {
    char body[1920];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    WriteLine(LevelTag(level), body);
}

void FatalExit(const char* fmt, ...) {
    char body[1920];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);
    WriteLine("[fatal] ", body);
    ::MessageBoxA(nullptr, body, "material-mixer", MB_OK | MB_ICONERROR);
    ::ExitProcess(1);
}

}  // namespace mm
