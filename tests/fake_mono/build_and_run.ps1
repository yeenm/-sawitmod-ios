# ============================================================================
#  build_and_run.ps1 -- offline self-test for the MonoHot / MonoCompat bridge
# ----------------------------------------------------------------------------
#  Builds tests/fake_mono/fake_dual_runtime.cpp together with the REAL
#  IL2CPP/MonoHot.cpp + IL2CPP/MonoCompat.cpp + IL2CPP/Il2CppApi.cpp against a
#  fake dlsym() that returns fake mono_* and il2cpp_* runtimes.  It verifies the
#  handle-dispatch logic (mono handles -> real mono_*, everything else -> the
#  original il2cpp shims) without an iPhone.
#
#  This is NOT part of scripts/check_link.ps1 (that one links platform_stubs.cpp,
#  which also defines dlsym; the two harnesses cannot share one link unit).
#
#  Usage:
#     powershell -ExecutionPolicy Bypass -File tests\fake_mono\build_and_run.ps1
#  Exit code 0 = all checks passed.
# ============================================================================

$ErrorActionPreference = "Stop"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Split-Path -Parent (Split-Path -Parent $here)   # ios/
Set-Location $root

$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    $vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
}
if (-not (Test-Path $vcvars)) {
    Write-Error "vcvars64.bat not found (need Visual Studio Build Tools)"
}

# Import the MSVC environment into this process so cl.exe and its libs resolve.
$envDump = cmd /c "call `"$vcvars`" >nul 2>&1 && set"
foreach ($line in $envDump) {
    if ($line -match '^([^=]+)=(.*)$') {
        [Environment]::SetEnvironmentVariable($matches[1], $matches[2], 'Process')
    }
}

$outDir = Join-Path $env:TEMP "sawit_fake_mono"
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$sources = @(
    (Join-Path $here "fake_dual_runtime.cpp"),
    (Join-Path $root "SawitMod\IL2CPP\MonoHot.cpp"),
    (Join-Path $root "SawitMod\IL2CPP\MonoCompat.cpp"),
    (Join-Path $root "SawitMod\IL2CPP\Il2CppApi.cpp")
)

Write-Host "== build fake dual-runtime harness =="
Push-Location $outDir
& cl.exe /nologo /utf-8 /std:c++17 /EHsc /W0 /MD /O1 `
    "/I$root\tests\stub" "/I$root\SawitMod" "/I$root\SawitMod\Includes" `
    "/I$root\SawitMod\ThirdParty" /Fo:.\ /Fe:.\fake_dual.exe @sources
$buildOk = ($LASTEXITCODE -eq 0) -and (Test-Path (Join-Path $outDir "fake_dual.exe"))
Pop-Location
if (-not $buildOk) {
    Write-Host "BUILD FAILED" -ForegroundColor Red
    exit 1
}

Write-Host "== run =="
& (Join-Path $outDir "fake_dual.exe")
$testExit = $LASTEXITCODE
if ($testExit -ne 0) {
    Write-Host "TEST FAILED (exit $testExit)" -ForegroundColor Red
} else {
    Write-Host "OK: fake dual-runtime dispatch test passed." -ForegroundColor Green
}
exit $testExit
