#pragma once
// 语法自检用的 strings.h（Windows 没有）。iOS 上是系统头，提供 strcasecmp。
#include <cstring>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
