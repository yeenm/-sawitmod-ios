#pragma once
// 语法自检用的 sys/time.h（Windows 没有）。
#include <cstdint>

struct timeval {
    long tv_sec;
    long tv_usec;
};

inline int gettimeofday(struct timeval *, void *) { return 0; }

#include <ctime>
inline struct tm *localtime_r(const time_t *t, struct tm *out) {
    return localtime_s(out, t) == 0 ? out : nullptr;
}
