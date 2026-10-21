// ============================================================================
//  DylibEntry.mm — dylib 入口
// ----------------------------------------------------------------------------
//  Android 版入口是 JNI_OnLoad（System.loadLibrary 触发）。iOS 的注入 dylib
//  在 dlopen 时执行 constructor，时机比 UIApplication 起来更早，所以真正的
//  启动编排放在 SMBootstrap::Start() 的后台线程里（见 App/Bootstrap.mm）。
//
//  不依赖 CydiaSubstrate/ElleKit：hook 走 Dobby，入口走 constructor。
// ============================================================================

#include "App/Bootstrap.h"

__attribute__((constructor)) static void SawitModEntry(void) {
    SMBootstrap::Start();
}
