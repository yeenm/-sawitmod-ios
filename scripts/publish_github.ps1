# ============================================================================
#  一条龙：在 Windows 上把本仓库推到 GitHub，让 Actions 帮你编 dylib
# ----------------------------------------------------------------------------
#  用法（在仓库根目录，也就是能看到 ios/ 和 .github/ 的那一层）：
#
#      powershell -ExecutionPolicy Bypass -File scripts\publish_github.ps1 `
#          -Owner <你的GitHub用户名> -Repo sawitmod-ios -Private
#
#  带 Token（推荐，能自动建仓库）：
#      $env:GITHUB_TOKEN = "ghp_xxx"        # 需要 repo 权限
#      powershell -ExecutionPolicy Bypass -File scripts\publish_github.ps1 `
#          -Owner <你> -Repo sawitmod-ios -Private
#
#  不带 Token 时脚本只做 git init/commit/push，仓库需要你先在网页上建好。
#
#  推完去 GitHub：Actions -> build-ios-dylib -> Run workflow -> 下载 dylib。
# ============================================================================
param(
    [Parameter(Mandatory = $true)][string]$Owner,
    [Parameter(Mandatory = $true)][string]$Repo,
    [string]$Root = "",
    [switch]$Private,
    [string]$Token = $env:GITHUB_TOKEN,
    [string]$Branch = "main"
)

$ErrorActionPreference = "Stop"

function Fail($msg) {
    Write-Host "[错误] $msg" -ForegroundColor Red
    exit 1
}
function Info($msg) { Write-Host "[*] $msg" -ForegroundColor Cyan }
function Ok($msg) { Write-Host "[OK] $msg" -ForegroundColor Green }

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    Fail "没找到 git。先装 Git for Windows：https://git-scm.com/download/win"
}

if ([string]::IsNullOrWhiteSpace($Root)) {
    $Root = (Get-Location).Path
}
$Root = (Resolve-Path $Root).Path
Set-Location $Root

if (-not (Test-Path (Join-Path $Root ".github\workflows"))) {
    Fail "当前目录不像仓库根：$Root（缺少 .github\workflows）。请在解压后的 SawitMod-ios-v1.1 目录里运行。"
}

# ---- 让仓库别把构建产物/大文件带上去 ----
$ignorePath = Join-Path $Root ".gitignore"
$ignoreLines = @(
    "# SawitMod 打包产物（源码仓库不需要）",
    ".theos/",
    "tests/build/",
    "dist/*.zip",
    "*.ipa",
    "*.dylib",
    "*.pdb",
    "*.obj",
    "__pycache__/",
    "*.pyc"
)
$existing = @()
if (Test-Path $ignorePath) { $existing = Get-Content $ignorePath }
$add = $ignoreLines | Where-Object { $existing -notcontains $_ }
if ($add.Count -gt 0) {
    Add-Content -Path $ignorePath -Value ("`n" + ($add -join "`n"))
    Info "已补充 .gitignore（构建产物不入库）"
}

# ---- git 初始化 ----
if (-not (Test-Path (Join-Path $Root ".git"))) {
    git init -q
    Info "git init"
}
git symbolic-ref HEAD "refs/heads/$Branch" 2>$null

if (-not (git config user.name)) { git config user.name "SawitMod Builder" }
if (-not (git config user.email)) { git config user.email "builder@users.noreply.github.com" }

git add -A
$hasHead = $true
git rev-parse --verify HEAD 2>$null | Out-Null
if ($LASTEXITCODE -ne 0) { $hasHead = $false }

$status = git status --porcelain
if ($status -or -not $hasHead) {
    git commit -q -m "SawitMod iOS: source + build workflows" 2>$null
    if ($LASTEXITCODE -ne 0) { Info "没有新的改动可提交" } else { Info "commit 完成" }
}

# ---- 远程仓库 ----
$remoteUrl = "https://github.com/$Owner/$Repo.git"
git remote remove origin 2>$null | Out-Null
git remote add origin $remoteUrl
Info "remote: $remoteUrl"

# ---- 用 Token 自动建仓库（可选）----
if (-not [string]::IsNullOrWhiteSpace($Token)) {
    $headers = @{
        Authorization = "token $Token"
        "User-Agent"  = "SawitMod-Publisher"
        Accept        = "application/vnd.github+json"
    }
    $body = @{ name = $Repo; private = [bool]$Private; auto_init = $false } | ConvertTo-Json
    try {
        Invoke-RestMethod -Method Post -Uri "https://api.github.com/user/repos" `
            -Headers $headers -ContentType "application/json" -Body $body | Out-Null
        Ok "GitHub 仓库已创建：$Owner/$Repo"
    } catch {
        Info "建仓接口返回：$($_.Exception.Message)"
        Info "（如果仓库已存在，这是正常的，继续 push）"
    }
}

# ---- push ----
Info "开始 push 到 $Branch（大文件多，第一次可能要几分钟）"
if (-not [string]::IsNullOrWhiteSpace($Token)) {
    # 用临时 header 传 token，不写进 .git/config
    $basic = [Convert]::ToBase64String([Text.Encoding]::ASCII.GetBytes("x-access-token:$Token"))
    git -c "http.extraheader=AUTHORIZATION: basic $basic" push -u origin $Branch
} else {
    git push -u origin $Branch
}

if ($LASTEXITCODE -ne 0) {
    Fail "push 失败。常见原因：仓库没建、分支保护、或需要登录（用 -Token 或先 git credential 登录）"
}

Ok "推送完成：https://github.com/$Owner/$Repo"
Write-Host ""
Write-Host "下一步：" -ForegroundColor Yellow
Write-Host "  1) 打开 https://github.com/$Owner/$Repo/actions"
Write-Host "  2) 左侧选 build-ios-dylib -> Run workflow"
Write-Host "  3) 跑完在 Artifacts 下载 SawitMod-dylib（Windows 用 Sideloadly 注入）"
Write-Host "  4) 或者先传一个 IPA 到 Release，再跑 build-and-inject 直接拿已注入 IPA"
