#pragma once
// 语法自检用的 sys/stat.h（Windows 没有）。
#include <cstdint>

struct stat {
    uint64_t st_size;
    int st_mode;
};

#define S_ISDIR(m) (((m) & 0170000) == 0040000)
#define S_ISREG(m) (((m) & 0170000) == 0100000)

inline int stat(const char *, struct stat *) { return -1; }
inline int mkdir(const char *, int) { return -1; }
