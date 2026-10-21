#pragma once
// 语法自检用的 sys/types.h（Windows 没有）。
#include <cstddef>
#include <cstdint>

typedef unsigned int socklen_t;
typedef long ssize_t;
typedef unsigned short sa_family_t;
typedef unsigned int in_addr_t;
typedef unsigned short in_port_t;
typedef unsigned short uint16_t_compat;
