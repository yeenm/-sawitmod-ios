// ============================================================================
//  stb_image_impl.cpp — stb_image 的唯一实现单元
// ----------------------------------------------------------------------------
//  Android 版把 `#define STB_IMAGE_IMPLEMENTATION` 放在 CyloneUI.h 里，只有
//  Main.cpp 包含它，所以没问题。iOS 里 Menu.cpp 与 MenuGlue.cpp 都要包含
//  ActiveUI（CyloneUI），实现放头文件会重复定义 stbi_* 符号 —— 挪到这里，
//  头文件只留声明。
// ============================================================================

#define STB_IMAGE_IMPLEMENTATION
#include "imgui/stb_image.h"
