#pragma once
// ============================================================================
//  JNIStuff.h — iOS 垫片
// ----------------------------------------------------------------------------
//  Android 版这个头里全是 JNI（取 ActivityThread、剪贴板、弹输入法）。iOS
//  没有 JVM，功能代码里真正还会用到的只有剪贴板，于是这里只保留同名函数：
//
//    getClipboardText()       -> UIPasteboard 文本
//    ShowSoftKeyboardInput()  -> no-op（iOS 用 UIAlertController 输入框，
//                                见 App/Alert.h 的 SMAlert::ShowInput）
//
//  原文件里的 getEnv()/getGlobalContext() 属于 Java 侧能力，iOS 不需要；
//  移植后的 ModUtils.cpp 已经删掉这些调用点。
// ============================================================================

#include <string>

#include "App/Platform.h"

inline const char *getClipboardText() {
    static std::string cached;
    cached = SMPlatform::Clipboard();
    return cached.c_str();
}

inline int ShowSoftKeyboardInput() {
    // iOS 侧输入走 SMAlert::ShowInput（UIAlertController），没有"切换输入法"这一步。
    return 0;
}
