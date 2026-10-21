#pragma once
// ============================================================================
//  Defaults.h — 设置持久化（替代 SharedPreferences / Preferences.java）
// ----------------------------------------------------------------------------
//  Java 版 Preferences 用 "功能编号 -> 值" 存；iOS 版直接按字符串键存到
//  NSUserDefaults，键名沿用 feature 编号的十进制字符串，语义不变。
// ============================================================================

#include <string>

namespace SMDefaults {

bool GetBool(const std::string &key, bool def = false);
void SetBool(const std::string &key, bool value);

int GetInt(const std::string &key, int def = 0);
void SetInt(const std::string &key, int value);

std::string GetString(const std::string &key, const std::string &def = "");
void SetString(const std::string &key, const std::string &value);

void Remove(const std::string &key);
void Synchronize();

}  // namespace SMDefaults
