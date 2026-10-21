#pragma once
// 语法自检用的 unistd.h（Windows 没有）。
#include <cstddef>
#include <cstdint>

typedef int pid_t;
typedef unsigned int useconds_t;
typedef long ssize_t;

#define _SC_PAGESIZE 29
#define _SC_PAGE_SIZE _SC_PAGESIZE
#define F_OK 0
#define R_OK 4
#define W_OK 2

inline unsigned int sleep(unsigned int) { return 0; }
inline int usleep(unsigned int) { return 0; }
inline long sysconf(int) { return 4096; }
inline int getpid() { return 0; }
inline int close(int) { return 0; }
inline ssize_t read(int, void *, size_t) { return 0; }
inline ssize_t write(int, const void *, size_t) { return 0; }
inline int access(const char *, int) { return 0; }
inline int unlink(const char *) { return 0; }
inline int rmdir(const char *) { return 0; }
inline char *getcwd(char *, size_t) { return nullptr; }
