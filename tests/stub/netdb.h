#pragma once
// 语法自检用的 netdb.h（Windows 没有）。
#include <cstddef>
#include <netinet/in.h>

#define AI_PASSIVE 0x0001

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

struct addrinfo {
    int ai_flags;
    int ai_family;
    int ai_socktype;
    int ai_protocol;
    size_t ai_addrlen;
    struct sockaddr *ai_addr;
    char *ai_canonname;
    struct addrinfo *ai_next;
};

inline int getaddrinfo(const char *, const char *, const struct addrinfo *,
                       struct addrinfo **) {
    return -1;
}
inline void freeaddrinfo(struct addrinfo *) {}
inline struct hostent *gethostbyname(const char *) { return nullptr; }
