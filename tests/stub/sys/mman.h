#pragma once
// 语法自检用的 sys/mman.h（Windows 没有）。
#include <cstddef>

#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02

inline void *mmap(void *, size_t, int, int, int, long) { return nullptr; }
inline int munmap(void *, size_t) { return 0; }
inline int mprotect(void *, size_t, int) { return 0; }
