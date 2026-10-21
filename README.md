# SawitMod — iOS 移植版

这是 `app/src/main/jni` 那套 **Android 原生悬浮菜单**（心动小镇 com.xd.xdt，ImGui 界面 + 卡密授权 + 云下发功能目录 + 托管反射）的 iOS 源码移植。

产物是一个**注入式 dylib**：把 `SawitMod.dylib` 塞进已解密的游戏 IPA，启动后菜单悬浮在游戏画面之上。不依赖 CydiaSubstrate/ElleKit（hook 用 Dobby，入口用 constructor），也不 hook 游戏的渲染管线（菜单画在自己的 `UIWindow` + `CAEAGLLayer` 上）。

```
ios/
├── Makefile                     Theos 构建（产物 SawitMod.dylib）
├── control                      包信息（可选，打 deb 用）
├── scripts/
│   ├── gen_state.py             从 State.h 生成 State.cpp 的全局定义
│   └── inject_ipa.sh            macOS：把 dylib 注入 IPA 并重签
├── tests/
│   └── crypto_test.cpp          SHA-256/HMAC/MD5/RC4/AES-GCM 测试向量自检
└── SawitMod/
    ├── App/                     iOS 平台层（窗口/GL/HTTP/弹窗/持久化/崩溃）
    ├── Core/                    Android 业务层的移植（State/Auth/Weiyan/...）
    ├── IL2CPP/                  il2cpp 符号解析 + mono_* 兼容层
    ├── UI/                      DrawMenu（原样移植）+ 字体/主题/水印
    ├── Includes/                日志/宏/工具垫片（Android 同名头）
    ├── KittyMemory/             内存读写/补丁（Mach 版）
    ├── Starcoolxdl/             xdl 兼容层（dyld + dlsym）
    ├── ByNameModding/           Tools（读写/Hook/特征码）
    ├── ThirdParty/
    │   ├── imgui/               ImGui 1.92.8 + GL3 后端 + CyloneUI
    │   └── Dobby/               放 libdobby.a（需自行编译，见 docs）
    └── Resources/               NotoSansSC / Roboto 字库
```

## 快速开始（macOS）

```bash
# 0) 装 Theos（已有可跳过）：git clone --recursive https://github.com/theos/theos.git ~/theos
export THEOS=~/theos

# 1) 一键：编 Dobby + 编 dylib
bash scripts/build_all.sh

# 2) 注入并重签
./scripts/inject_ipa.sh ~/xdt.ipa .theos/obj/SawitMod.dylib ~/xdt-mod.ipa
```

没有 Mac：
- 只编 dylib：跑 `.github/workflows/build-ios.yml`，下载 `SawitMod.dylib`，用 Windows 版 Sideloadly 的 `Advanced Options -> +dylib/deb` 注入；
- 直接出已注入包：把已解密 IPA 传到自己仓库的 Release，跑 `.github/workflows/build-and-inject.yml`，下载 `SawitMod-injected.ipa` 后用 Sideloadly 安装。
发布脚本：`scripts/publish_github.ps1`（git init + 建仓 + push 一条龙）。详见包根目录 `PACKAGE.md`。

## 原样搬过来的部分

- `UI/Menu.cpp` = Android `Main.cpp` 第 2451–5296 行（DrawMenu），一行未改
- `Core/FramePump.cpp` = `Main.cpp` 第 1890–2449 行（帧泵 + ESP）
- `Core/FeatureDispatch.cpp` = `Main.cpp` 第 617–1150 行（动态调用 + 方法补丁）
- `ModFeatures.cpp` / `EmoteUnlock.cpp` / `AutoDraw.cpp` … 只改了平台头文件
- `ThirdParty/imgui`、`Resources/*.h` 直接复制

## 需要按 iOS 包重新标定的部分

`Core/Offsets-iOS.h` 里所有 `0x0` 都是**未标定哨兵**（原 Android RVA 不能复用）。标定前：

- 菜单、卡密、公告、云下发目录、免费功能的界面都正常
- 依赖 RVA 的 native hook（`GameApp.Update` 帧泵、Themis/SDK 补丁）会自动跳过
- 托管反射类功能依赖 il2cpp 的类名/方法名（与 Android 同版本一致）与字段偏移

标定步骤见 `docs/移植说明.md`。

## 本机验证（交付时已跑过）

| 检查 | 覆盖 | 结果 |
|---|---|---|
| `tests/crypto_test.cpp` | SHA-256 / HMAC / MD5 / RC4 / AES-256-GCM 测试向量 | 14/14 通过 |
| `scripts/check_link.ps1` | 全部平台无关 C++（状态/授权/卡密/payload/目录/功能/反射/菜单/ImGui）编译 + 链接 | 通过 |

Windows 上无法编译 ObjC++（UIKit/GLES），这部分需要在 macOS + Theos 里编一次。

## 目录补充

- `SawitMod/ThirdParty/Dobby/` — 放你编出来的 `libdobby.a`（有 README 说明）
- `tests/` — 自检程序与 iOS 头文件的 Windows 桩，不参与 iOS 构建

## 文档

- [`docs/移植说明.md`](docs/移植说明.md) — 架构对照、逐模块改造点、标定流程、已知限制
- [`docs/API映射.md`](docs/API映射.md) — Android → iOS 的 API 对照表
- [`docs/构建与注入.md`](docs/构建与注入.md) — Theos/Dobby/签名/安装/排错
- [`tests/README.md`](tests/README.md) — 两个自检程序怎么跑
