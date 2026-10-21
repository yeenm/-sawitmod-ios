#pragma once
// 语法自检用的 sys/socket.h（Windows 没有）。
#include <cstdint>
#include <sys/types.h>

struct sockaddr;

#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define SOL_SOCKET 0xffff
#define SO_REUSEADDR 0x0004
#define SO_RCVTIMEO 0x1006
#define SO_SNDTIMEO 0x1005
#define MSG_NOSIGNAL 0x4000

inline int socket(int, int, int) { return -1; }
inline int connect(int, const struct sockaddr *, socklen_t) { return -1; }
inline int send(int, const void *, size_t, int) { return -1; }
inline int recv(int, void *, size_t, int) { return -1; }
inline int setsockopt(int, int, int, const void *, socklen_t) { return -1; }
inline int shutdown(int, int) { return -1; }
