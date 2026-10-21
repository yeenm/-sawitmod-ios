#pragma once
// 语法自检用的 mach-o/dyld.h（Windows 没有）。
#include <cstdint>

extern "C" uint32_t _dyld_image_count(void);
extern "C" const char *_dyld_get_image_name(uint32_t);
extern "C" const void *_dyld_get_image_header(uint32_t);
extern "C" intptr_t _dyld_get_image_vmaddr_slide(uint32_t);
