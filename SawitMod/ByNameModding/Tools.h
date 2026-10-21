#pragma once
// ============================================================================
//  ByNameModding/Tools.h — iOS 版（保持 Android 版命名空间/函数名）
// ----------------------------------------------------------------------------
//  Android 版这里带着 JNIEnv / GetPackageName；iOS 没有 JVM，去掉那一个函数，
//  其余（读/写/改保护/Hook/找镜像/找特征码）照旧，实现见 Tools.cpp。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include "Includes/Dobby/dobby.h"

namespace Tools {
void Hook(void *target, void *replace, void **backup);
bool Read(void *addr, void *buffer, size_t length);
bool Write(void *addr, void *buffer, size_t length);
bool ReadAddr(void *addr, void *buffer, size_t length);
bool WriteAddr(void *addr, void *buffer, size_t length);

bool PVM_ReadAddr(void *addr, void *buffer, size_t length);
bool PVM_WriteAddr(void *addr, void *buffer, size_t length);

bool IsPtrValid(void *addr);

uintptr_t GetBaseAddress(const char *name);
uintptr_t GetEndAddress(const char *name);
uintptr_t FindPattern(const char *lib, const char *pattern);

std::string RandomString(const int len);
}  // namespace Tools
