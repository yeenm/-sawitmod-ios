#pragma once
// 语法自检用的 dirent.h（Windows 没有）。
#include <cstddef>

struct DIR;
struct dirent {
    char d_name[256];
    unsigned char d_type;
};

#define DT_REG 8
#define DT_DIR 4

inline DIR *opendir(const char *) { return nullptr; }
inline struct dirent *readdir(DIR *) { return nullptr; }
inline int closedir(DIR *) { return 0; }
