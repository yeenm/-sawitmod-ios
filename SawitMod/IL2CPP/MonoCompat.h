#pragma once
// ============================================================================
//  MonoCompat.h — 把 Android 代码里的 mono_* 反射 API 垫到 il2cpp 上
// ----------------------------------------------------------------------------
//  这份源码里所有"操作托管类"的功能代码（ModFeatures.cpp / ModUtils.cpp /
//  EmoteUnlock.cpp ...）都是按 Android 版的运行时写的：那边游戏逻辑是进程内
//  加载的 Mono 程序集，所以调 mono_class_from_name / mono_runtime_invoke。
//
//  iOS 上 Unity 只有 il2cpp（AOT，无法加载 Mono 程序集），但**类名、命名空间、
//  方法名、字段名与字段布局在同一个游戏的同版本里一致**。所以这里给出一层
//  mono -> il2cpp 的适配：
//
//    mono_image_loaded            -> 按名字在 il2cpp domain 的 assembly 里找 image
//    mono_class_from_name         -> il2cpp_class_from_name
//    mono_class_get_method_from_name -> il2cpp_class_get_method_from_name
//    mono_runtime_invoke          -> il2cpp_runtime_invoke
//    mono_field_*                 -> il2cpp_field_*
//    mono_class_get_methods/fields-> il2cpp 同名迭代器
//    mono_gchandle_*              -> il2cpp_gchandle_*
//
//  无法等价映射的（泛型膨胀、assembly 打开、vtable）返回 nullptr/no-op：
//  对应用途的代码本来就要按 iOS 版本重写，硬造一个假实现只会埋更深的坑。
//
//  安装：Bootstrap 在 il2cpp 就绪后调用 SMMono::InstallPointers()，把垫片写进
//  ModUtils.h 里那批 _monoXxx 全局指针（Android 版由 SetupMono 填）。
// ============================================================================

#include <cstdint>

namespace SMMono {

// 把 mono_* 垫片装进 _monoXxx 全局指针（可重复调用，幂等）。
void InstallPointers();

// 安装是否完成（指针已指向 il2cpp 垫片）。
bool Installed();

// il2cpp 运行时是否已可用（核心符号齐全）。
bool RuntimeReady();

// mono_* 符号名 -> 垫片函数地址；不认识返回 nullptr。
void *Lookup(const char *symbolName);

// 诊断：当前是否拿得到 root domain / 主程序集。
bool Probe();

}  // namespace SMMono

// xdl 兼容层用这个入口（见 Starcoolxdl/xdl.cpp）。
void *SMMonoLookup(const char *symbolName);
