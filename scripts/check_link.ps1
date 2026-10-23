# ============================================================================
#  check_link.ps1 — 在 Windows 上给 iOS 版做"纯 C++ 部分"的编译 + 链接自检
# ----------------------------------------------------------------------------
#  为什么能用 Windows 跑：
#    * 业务代码（State/Auth/Weiyan/ModFeatures/ModUtils/...）是平台无关 C++，
#      平台差异全部收敛在 App/*.mm（ObjC++，iOS 专用，不参与本检查）
#    * tests/stub/ 提供 mach/POSIX 头的空实现，tests/platform_stubs.cpp 提供
#      SMPlatform/SMHttp/SMCrypto(ECDH) 等符号的桩
#  这能抓到的错误：
#    * 语法/类型错误（MSVC 与 clang 的差异仍需 iOS 侧再编一次）
#    * "声明了但没人定义"的符号（跨模块漏实现）
#  抓不到的：ObjC++ 文件、GLES/UIKit 相关代码 —— 那些只能在 macOS + Theos 里编。
#
#  用法： powershell -ExecutionPolicy Bypass -File scripts\check_link.ps1
# ============================================================================

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $vcvars)) {
    Write-Error "找不到 vcvars64.bat（需要 Visual Studio Build Tools）"
}

$sources = @(
    "SawitMod\Core\State.cpp",
    "SawitMod\Core\Crypto.cpp",
    "SawitMod\Core\FramePump.cpp",
    "SawitMod\Core\AntiBan.cpp",
    "SawitMod\Core\FeatureDispatch.cpp",
    "SawitMod\IL2CPP\Il2CppApi.cpp",
    "SawitMod\IL2CPP\MonoCompat.cpp",
    "SawitMod\IL2CPP\MonoHot.cpp",
    "SawitMod\ModUtils.cpp",
    "SawitMod\ModFeatures.cpp",
    "SawitMod\NetAuth.cpp",
    "SawitMod\PayloadVM.cpp",
    "SawitMod\LocalCatalog.cpp",
    "SawitMod\ClientControlAgent.cpp",
    "SawitMod\Weiyan.cpp",
    "SawitMod\EmoteUnlock.cpp",
    "SawitMod\FriendInteractUnlock.cpp",
    "SawitMod\AutoDraw.cpp",
    "SawitMod\MonoSymbolProbe.cpp",
    "SawitMod\WeatherChanger.cpp",
    "SawitMod\PlayerAction.cpp",
    "SawitMod\CameraMod.cpp",
    "SawitMod\GachaSelector.cpp",
    "SawitMod\ByNameModding\Tools.cpp",
    "SawitMod\Starcoolxdl\xdl.cpp",
    "SawitMod\KittyMemory\MemoryPatch.cpp",
    "SawitMod\KittyMemory\KittyUtils.cpp",
    "SawitMod\ThirdParty\imgui\imgui.cpp",
    "SawitMod\ThirdParty\imgui\imgui_draw.cpp",
    "SawitMod\ThirdParty\imgui\imgui_tables.cpp",
    "SawitMod\ThirdParty\imgui\imgui_widgets.cpp",
    "SawitMod\UI\Menu.cpp",
    "SawitMod\UI\MenuGlue.cpp",
    "tests\kitty_stub.cpp",
    "tests\platform_stubs.cpp",
    "tests\link_main.cpp"
)

New-Item -ItemType Directory -Force -Path tests\build | Out-Null

$cl = @"
call "$vcvars" >nul
cd /d $root
cl /nologo /utf-8 /std:c++17 /EHsc /W0 /MD /O1 /D__aarch64__ /DIMGUI_IMPL_OPENGL_ES3=1 /DGL_SILENCE_DEPRECATION=1 ^
   /I tests\stub /I SawitMod /I SawitMod\Includes /I SawitMod\ThirdParty /I SawitMod\ThirdParty\imgui ^
   /Fo:tests\build\ /Fe:tests\build\link_check.exe ^
   $($sources -join ' ')
"@
Set-Content -Path tests\build\build.cmd -Value $cl -Encoding ASCII

Write-Host "== 编译 + 链接（业务代码 C++ 部分）=="
& cmd /c tests\build\build.cmd
if ($LASTEXITCODE -ne 0) {
    Write-Host "
链接/编译失败：上面的 undefined external 就是缺失的实现。" -ForegroundColor Red
    exit $LASTEXITCODE
}
Write-Host "
OK：全部目标文件编译并链接通过。" -ForegroundColor Green
& tests\build\link_check.exe
