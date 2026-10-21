# Android → iOS API 对照表

移植时逐条替换用的清单。表里的 iOS 实现在 `ios/SawitMod/` 下都有对应源码。

---

## 1. Java / JNI

| Android | iOS 替代 | 位置 |
|---|---|---|
| `System.loadLibrary` / `JNI_OnLoad` | `__attribute__((constructor))` | `App/DylibEntry.mm` |
| `JavaVM` / `JNIEnv` / `jobject` | 无（C++ 直接调 Foundation） | —— |
| `Activity` / `Service` / `Context` | `UIWindow` / `UIViewController` | `App/OverlayWindow.mm` |
| `WindowManager` 悬浮窗 + `SurfaceView` | `UIWindow(level=Alert+100)` + `CAEAGLLayer` | `App/OverlayWindow.mm` |
| `MotionEvent` / hook `libinput` | `hitTest` 穿透 + `touchesBegan/Moved/Ended` | `App/OverlayWindow.mm` |
| `AlertDialog` + `EditText`（卡密输入） | `UIAlertController` + `addTextField` | `App/Alert.mm` |
| `Toast` | 自绘 `UILabel` 浮层（1.6s 淡出） | `App/OverlayWindow.mm` |
| `SharedPreferences` | `NSUserDefaults` | `App/Defaults.mm` |
| `Settings.Secure.ANDROID_ID` | Keychain 持久 UUID（回落 `identifierForVendor`） | `App/Platform.mm` |
| `ClipboardManager` | `UIPasteboard` | `App/Platform.mm` |
| `Intent(ACTION_VIEW)` | `UIApplication openURL:` | `App/Platform.mm` |
| `Process.killProcess` / `_exit` | `_exit(0)`（主线程） | `App/Platform.mm` |
| `Thread.setDefaultUncaughtExceptionHandler` | `NSSetUncaughtExceptionHandler` + 信号 | `App/CrashHandler.mm` |
| `getFilesDir()` / `getExternalFilesDir()` | `NSSearchPathForDirectoriesInDomains` | `App/Platform.mm` |

## 2. 路径

| Android | iOS |
|---|---|
| `/data/data/<pkg>/files/` | `SMPlatform::DataDir()` = `Library/Application Support/SawitMod` |
| `/storage/emulated/0/Documents/` | `SMPlatform::DocumentDir()` |
| `/sdcard/Android/data/<pkg>/files/Drawings/` | `Documents/Drawings/` |
| `/sdcard/.../Instrument/` | `Documents/Instrument/` |
| `/sdcard/.../Photos/` | `Documents/Photos/` |

## 3. 加密（原：libcrypto.so via xdl）

| Android | iOS |
|---|---|
| `SHA256(data, len, out)` | `SMCrypto::Sha256Raw(data, out)` |
| `HMAC(EVP_sha256(), key, ...)` | `SMCrypto::HmacSha256Raw(key, msg, len, out)` |
| `EC_KEY_new_by_curve_name(NID_X9_62_prime256v1)` + `EC_KEY_generate_key` | `SMCrypto::EcdhGenerateEphemeral(outPubHex)` |
| `ECDH_compute_key` | `SMCrypto::EcdhComputeShared(key, serverPubHex, out32)` |
| `EC_KEY_free` | `SMCrypto::EcdhFree(key)` |
| `ECDSA_do_verify(hash, 32, sig, key)`（P-1363 r‖s） | `SMCrypto::EcdsaVerifyP256(pubXY, hash, sigRS)` |
| `EVP_aes_256_gcm` 解密 | `SMCrypto::Aes256GcmDecrypt(key, iv, cipher, tag, out)` |
| `MD5(...)`（微验签名） | `SMCrypto::Md5Hex(data)` |
| `RC4(...)`（微验报文） | `SMCrypto::Rc4(data, key)` |

## 4. 网络

| Android | iOS |
|---|---|
| `HttpURLConnection` POST JSON（授权/心跳/payload） | `SMHttp::PostJson(url, json, 8)`（`NSURLSession` 同步封装） |
| `HttpURLConnection` 读 401/403 响应体 | `SMHttp::Post(...)` 返回 `Response{code, body, transportOk}` |
| 微验卡密的裸 socket + 阿里 DNS TCP 兜底 | 保持不变（BSD socket 在 iOS 可用，ATS 不拦裸 socket） |
| Java `Scanner` 读流 | `recv()` 循环 + chunked 解析（原样） |

## 5. 运行时 / Hook

| Android | iOS |
|---|---|
| `xdl_open("libil2cpp.so")` | `SMIL2CPP::Get()`（`dlsym(RTLD_DEFAULT, "il2cpp_*")`） |
| `xdl_sym(handle, "il2cpp_class_from_name")` | `SMIL2CPP::Symbol("il2cpp_class_from_name")` |
| `fake_dlfcn` / `ByNameModding` 符号扫描 | `SMIL2CPP::FindImage` / `FindClassAnywhere` |
| `mono_*`（libmonosgen） | `IL2CPP/MonoCompat.cpp` 映射到 `il2cpp_*` |
| `A64HookFunction` / `MSHookFunction` | `DobbyHook(addr, replace, &orig)` |
| `DobbySymbolResolver` | 同名，Dobby 提供 |
| `getAbsoluteAddress("libil2cpp.so", rva)` | `SMImage::FindImageBase("UnityFramework") + rva` |
| `/proc/self/maps` 找基址 | `_dyld_image_count/_dyld_get_image_header` |
| `mprotect`（KittyMemory） | `vm_protect(mach_task_self(), ...)` + `__builtin___clear_cache` |
| `pthread_create` 启动 `hack_thread` | `std::thread(...).detach()`（`SMBootstrap::Start`） |
| `eglSwapBuffers` hook | 不需要（自己的 `CADisplayLink` 渲染循环） |
| `ANativeWindow_getWidth/Height` | `glGetRenderbufferParameteriv(GL_RENDERBUFFER_*)` |
| `__android_log_print` / `LOGI` | `Includes/Logger.h`（默认空宏，`SM_ENABLE_LOG=1` 走 `NSLog`） |

## 6. 线程

| Android | iOS |
|---|---|
| 主线程（Java UI） | 主线程（`dispatch_get_main_queue`） |
| `AttachCurrentThread`（JNI） | `il2cpp_thread_attach(il2cpp_domain_get())`，`SMIL2CPP::EnsureThreadAttached()` |
| `RunOnUnityThreadSync/Async`（自建队列） | 不变（`ModUtils.cpp` 原样保留） |
| `Handler.postDelayed` | `dispatch_after` / `CADisplayLink` |

## 7. 界面

| Android | iOS |
|---|---|
| ImGui + `imgui_impl_opengl3`（GLES2，`#version 100`） | 同一套 ImGui 1.92.8，`IMGUI_IMPL_OPENGL_ES3`（`#version 300 es`） |
| `CyloneUI`（`ActiveUI`） | 原样复制（只改 `Version.h` / 品牌头的 include 路径） |
| `NotoSansSC_Regular`（base64/数组） | 同一个头文件，直接 `sizeof` 传给 `SetupFonts` |
| DPI：屏幕短边 / 1080 | 不变（用 renderbuffer 像素尺寸） |
| 简化条 / 拖动 / 缩放 | 原样（ImGui 逻辑与平台无关） |
