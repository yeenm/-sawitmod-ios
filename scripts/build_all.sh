#!/usr/bin/env bash
# ============================================================================
#  build_all.sh — macOS 一键打包（Dobby + dylib）
# ----------------------------------------------------------------------------
#  用法：
#      export THEOS=~/theos
#      bash scripts/build_all.sh
#
#  产物：
#      .theos/obj/SawitMod.dylib  （Theos 版本不同也可能是 .theos/obj/debug/...）
#
#  说明：
#    * 第一次跑会自动 clone 并编译 Dobby 的 iphoneos 静态库（arm64 + arm64e）
#    * 已经存在 SawitMod/ThirdParty/Dobby/libdobby.a 时跳过 Dobby 编译
#    * 需要：Xcode 命令行工具（xcrun）、cmake、python3、make、Theos
# ============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"          # -> ios/
DYLIB_DST="$ROOT/SawitMod/ThirdParty/Dobby/libdobby.a"
DOBBY_REPO="${DOBBY_REPO:-https://github.com/jmpews/Dobby.git}"

say() { printf "\n\033[1;36m[%s]\033[0m %s\n" "$1" "$2"; }
die() { printf "\n\033[1;31m[错误]\033[0m %s\n" "$1"; exit 1; }

need() { command -v "$1" >/dev/null 2>&1 || die "缺少命令：$1"; }
need xcrun
need cmake
need python3
need make
need git

if [[ -z "${THEOS:-}" ]]; then
  for cand in "$HOME/theos" "/opt/theos" "/opt/homebrew/theos"; do
    [[ -d "$cand" ]] && export THEOS="$cand" && break
  done
fi
[[ -n "${THEOS:-}" && -d "${THEOS:-}" ]] || die "未找到 Theos：请先 export THEOS=~/theos（安装方法见 docs/构建与注入.md）"

say "1/3" "准备 Dobby（iOS arm64/arm64e 静态库）"
if [[ -f "$DYLIB_DST" ]]; then
  echo "已存在：$DYLIB_DST（跳过）"
else
  WORK="${TMPDIR:-/tmp}/dobby-build-$$"
  rm -rf "$WORK"
  git clone --depth=1 --recursive "$DOBBY_REPO" "$WORK"
  (
    cd "$WORK"
    python3 scripts/platform_builder.py --platform=iphoneos --arch=all
  )
  SRC=""
  for f in \
    "$WORK/build/iphoneos/universal/libdobby.a" \
    "$WORK/build/iphoneos/arm64/libdobby.a"; do
    if [[ -f "$f" ]]; then SRC="$f"; break; fi
  done
  [[ -n "$SRC" ]] || die "Dobby 编译失败：没有找到 libdobby.a（看上面的编译输出）"
  mkdir -p "$(dirname "$DYLIB_DST")"
  cp "$SRC" "$DYLIB_DST"
  echo "已安装：$DYLIB_DST（来源：$SRC）"
fi

say "2/3" "编译 SawitMod.dylib"
cd "$ROOT"
make clean >/dev/null 2>&1 || true
make -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

say "3/3" "完成"
FOUND="$(find "$ROOT/.theos" -name 'SawitMod.dylib' -type f 2>/dev/null | head -n 3 || true)"
if [[ -z "$FOUND" ]]; then
  FOUND="$(find "$ROOT/.theos" -name '*.dylib' -type f 2>/dev/null | head -n 3 || true)"
fi
if [[ -z "$FOUND" ]]; then
  die "没有找到产物 dylib，请检查 make 输出"
fi
echo "$FOUND"
echo
echo "下一步（注入 IPA 并重签）："
echo "    ./scripts/inject_ipa.sh <原始已解密.ipa> $(echo "$FOUND" | head -n1) <输出.ipa>"
echo "或者把 dylib 交给 Sideloadly：Advanced Options -> +dylib/deb"
