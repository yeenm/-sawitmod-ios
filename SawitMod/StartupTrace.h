#pragma once

// Logger diagnosis startup dimatikan pada build normal/release.
#ifndef SAWIT_STARTUP_TRACE
#define SAWIT_STARTUP_TRACE 0
#endif

#if SAWIT_STARTUP_TRACE
#include "Includes/Logger.h"
#include <atomic>
#include <chrono>
#include <cstdio>

namespace StartupTrace {
inline long long ElapsedMs() {
    static const auto start = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
}

inline void Mark(const char* stage) {
    static std::atomic<unsigned int> sequence{0};
    char line[192]{};
    std::snprintf(line, sizeof(line), "#%u +%lldms %s",
                  sequence.fetch_add(1, std::memory_order_relaxed) + 1,
                  ElapsedMs(), stage ? stage : "(null)");
    LOGI("SAWITSTART %s", line);
}
} // namespace StartupTrace

#define STARTUP_TRACE(stage) ::StartupTrace::Mark(stage)
#else
#define STARTUP_TRACE(stage) ((void)0)
#endif
