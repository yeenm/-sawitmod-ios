#pragma once
// 语法自检用的 fcntl.h（Windows 没有）。
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_TRUNC 01000
#define O_CLOEXEC 02000000
inline int open(const char *, int, ...) { return -1; }
