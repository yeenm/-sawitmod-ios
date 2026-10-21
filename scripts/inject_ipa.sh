#!/usr/bin/env bash
# ============================================================================
#  inject_ipa.sh — 把 SawitMod.dylib 注入到已解密的 iOS 游戏 IPA 里
# ----------------------------------------------------------------------------
#  只在 macOS 上跑（需要 Xcode 的 codesign + insert_dylib/optool）。
#  用法：
#     ./scripts/inject_ipa.sh <原始.ipa> <SawitMod.dylib> <输出.ipa> [签名身份]
#
#  例：
#     ./scripts/inject_ipa.sh ~/xdt.ipa .theos/obj/SawitMod.dylib ~/xdt-mod.ipa
#     ./scripts/inject_ipa.sh ~/xdt.ipa SawitMod.dylib out.ipa "Apple Development: me"
#
#  注意：
#    * 目标 IPA 必须是**已解密**的（App Store 的二进制是加密的，注入后无法运行）
#    * 注入后必须重新签名，否则安装即闪退
#    * 没有 insert_dylib 时脚本会提示安装（brew install insert_dylib 或从
#      https://github.com/Tyilo/insert_dylib 编译）
# ============================================================================
set -euo pipefail

IPA_IN="${1:-}"
DYLIB="${2:-}"
IPA_OUT="${3:-}"
IDENTITY="${4:--}"

if [[ -z "$IPA_IN" || -z "$DYLIB" || -z "$IPA_OUT" ]]; then
  echo "usage: $0 <input.ipa> <SawitMod.dylib> <output.ipa> [codesign-identity]" >&2
  exit 1
fi
[[ -f "$IPA_IN" ]] || { echo "找不到输入 IPA: $IPA_IN" >&2; exit 1; }
[[ -f "$DYLIB" ]] || { echo "找不到 dylib: $DYLIB" >&2; exit 1; }

# insert_dylib：优先 PATH，其次常见自编译路径
INSERT_DYLIB="$(command -v insert_dylib || true)"
[[ -z "$INSERT_DYLIB" && -x "/usr/local/bin/insert_dylib" ]] && INSERT_DYLIB=/usr/local/bin/insert_dylib
if [[ -z "$INSERT_DYLIB" ]]; then
  echo "缺少 insert_dylib。安装：brew install insert_dylib 或从 Tyilo/insert_dylib 编译。" >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "[1/6] 解压 IPA"
unzip -q "$IPA_IN" -d "$WORK"

APP_DIR="$(find "$WORK/Payload" -maxdepth 1 -name '*.app' -type d | head -n 1)"
[[ -n "$APP_DIR" ]] || { echo "Payload 里没有 .app" >&2; exit 1; }
APP_NAME="$(basename "$APP_DIR" .app)"
BIN="$APP_DIR/$APP_NAME"
[[ -f "$BIN" ]] || { echo "找不到主二进制: $BIN" >&2; exit 1; }

echo "[2/6] 拷贝 dylib -> $APP_NAME.app/"
cp "$DYLIB" "$APP_DIR/SawitMod.dylib"

echo "[3/6] 注入 LC_LOAD_DYLIB"
"$INSERT_DYLIB" --strip-codesig --all-yes --inplace "@executable_path/SawitMod.dylib" "$BIN"

echo "[4/6] 重签名 frameworks / dylib"
find "$APP_DIR" -name '*.framework' -maxdepth 2 -type d | while read -r fw; do
  name="$(basename "$fw" .framework)"
  [[ -f "$fw/$name" ]] && codesign -f -s "$IDENTITY" --timestamp=none "$fw/$name" || true
done
codesign -f -s "$IDENTITY" --timestamp=none "$APP_DIR/SawitMod.dylib"

echo "[5/6] 重签主 App（保留 entitlements）"
if [[ -f "$APP_DIR/embedded.mobileprovision" ]] && [[ "$IDENTITY" != "-" ]]; then
  security cms -D -i "$APP_DIR/embedded.mobileprovision" > "$WORK/profile.plist"
  /usr/libexec/PlistBuddy -x -c 'Print:Entitlements' "$WORK/profile.plist" > "$WORK/ent.plist"
  codesign -f -s "$IDENTITY" --entitlements "$WORK/ent.plist" --timestamp=none "$APP_DIR"
else
  codesign -f -s "$IDENTITY" --timestamp=none "$APP_DIR"
fi

echo "[6/6] 打包 $IPA_OUT"
rm -f "$IPA_OUT"
( cd "$WORK" && zip -qry "$IPA_OUT" Payload )
echo "完成：$IPA_OUT"
echo "提示：用 TrollStore / AltStore / SideStore 安装；首次启动若闪退，先看 ~/Documents/mod_menu_crash_*.txt。"
