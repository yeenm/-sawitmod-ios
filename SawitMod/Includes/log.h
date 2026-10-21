#pragma once
// Android 版 Includes/log.h 的等价物：只保证原有宏名可用。
#include "Includes/Logger.h"

#define LOG_TAG "SawitMod-iOS"

#ifndef ALOGI
#define ALOGI(...) LOGI(__VA_ARGS__)
#endif
#ifndef ALOGE
#define ALOGE(...) LOGE(__VA_ARGS__)
#endif

// Android 工程里 `#define __android_log_print(...) ((void)0)` 的 iOS 等价物。
#ifndef __android_log_print
#define __android_log_print(...) ((void)0)
#endif
#define ANDROID_LOG_INFO 4
#define ANDROID_LOG_ERROR 6
