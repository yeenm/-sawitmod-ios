#pragma once
// 语法自检用的 netinet/in.h（Windows 没有）。
#include <cstdint>
#include <sys/types.h>

#define AF_INET 2
#define AF_UNSPEC 0
#define IPPROTO_TCP 6
#define INADDR_ANY 0u
#define INADDR_NONE 0xFFFFFFFFu

struct in_addr {
    uint32_t s_addr;
};

struct sockaddr {
    sa_family_t sa_family;
    char sa_data[14];
};

struct sockaddr_in {
    sa_family_t sin_family;
    in_port_t sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

inline uint16_t htons(uint16_t v) { return v; }
inline uint16_t ntohs(uint16_t v) { return v; }
inline uint32_t htonl(uint32_t v) { return v; }
inline uint32_t ntohl(uint32_t v) { return v; }
