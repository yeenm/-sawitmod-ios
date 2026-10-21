#pragma once
// 语法自检用的 arpa/inet.h（Windows 没有）。
#include <netinet/in.h>

inline in_addr_t inet_addr(const char *) { return 0; }
inline char *inet_ntoa(struct in_addr) { return nullptr; }
inline int inet_pton(int, const char *, void *) { return 0; }

#include <cstdio>
inline const char *inet_ntop(int, const void *, char *dst, socklen_t size) {
    if (size) dst[0] = 0;
    return dst;
}
