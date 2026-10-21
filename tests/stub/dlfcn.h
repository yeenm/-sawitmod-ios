#pragma once
// 语法自检用的 dlfcn.h（Windows 没有）。
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_DEFAULT ((void *)0)
extern "C" void *dlopen(const char *, int);
extern "C" void *dlsym(void *, const char *);
extern "C" int dlclose(void *);
extern "C" char *dlerror(void);
