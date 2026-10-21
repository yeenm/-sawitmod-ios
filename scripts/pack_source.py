#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 ios/ 打成发布用的源码包。

用法： python scripts/pack_source.py
产物： dist/SawitMod-ios-v<版本>-src.zip
       dist/SHA256SUMS.txt

排除构建产物与自检临时目录，保留脚本的可执行位（zip 的 unix mode）。
"""
import hashlib
import os
import shutil
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # ios/
DIST = os.path.join(os.path.dirname(ROOT), "dist")
VERSION = "v1.1"
ZIP_NAME = f"SawitMod-ios-{VERSION}-src.zip"

EXCLUDE_DIRS = {
    ".theos", "build", "__pycache__", ".git", "DerivedData",
}
EXCLUDE_SUFFIX = {".obj", ".o", ".exe", ".pdb", ".ilk", ".zip", ".ipa", ".dylib", ".a"}
EXCLUDE_FILES = {".DS_Store"}


def should_skip(path: str, name: str) -> bool:
    parts = set(os.path.relpath(path, ROOT).split(os.sep))
    if parts & EXCLUDE_DIRS:
        return True
    if name in EXCLUDE_FILES:
        return True
    if os.path.splitext(name)[1].lower() in EXCLUDE_SUFFIX:
        return True
    return False


def add_dir(zf: zipfile.ZipFile, src_dir: str, arc_prefix: str) -> int:
    count = 0
    for dirpath, dirnames, filenames in os.walk(src_dir):
        dirnames[:] = [d for d in dirnames if d not in EXCLUDE_DIRS]
        for fname in sorted(filenames):
            full = os.path.join(dirpath, fname)
            if should_skip(full, fname):
                continue
            rel = os.path.relpath(full, src_dir)
            arc = os.path.join(arc_prefix, rel).replace(os.sep, "/")
            info = zipfile.ZipInfo(arc)
            info.date_time = time.localtime(os.path.getmtime(full))[:6]
            mode = 0o755 if fname.endswith((".sh", ".py")) else 0o644
            info.external_attr = (0o100000 | mode) << 16
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(full, "rb") as f:
                zf.writestr(info, f.read())
            count += 1
    return count


def sha256(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main() -> None:
    os.makedirs(DIST, exist_ok=True)
    zip_path = os.path.join(DIST, ZIP_NAME)
    if os.path.exists(zip_path):
        os.remove(zip_path)

    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        count = add_dir(zf, ROOT, f"SawitMod-ios-{VERSION}")
        # 附上 GitHub Actions 工作流（编 dylib / 直接注入 IPA），
        # 方便没 Mac 的人推到 GitHub 上跑。
        wf_dir = os.path.join(os.path.dirname(ROOT), ".github", "workflows")
        if os.path.isdir(wf_dir):
            for name in sorted(os.listdir(wf_dir)):
                if not name.endswith((".yml", ".yaml")):
                    continue
                src = os.path.join(wf_dir, name)
                info = zipfile.ZipInfo(f"SawitMod-ios-{VERSION}/.github/workflows/{name}")
                info.date_time = time.localtime(os.path.getmtime(src))[:6]
                info.external_attr = (0o100000 | 0o644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(src, "rb") as f:
                    zf.writestr(info, f.read())
                count += 1
        readme = os.path.join(DIST, "PACKAGE.md")
        if os.path.exists(readme):
            zf.write(readme, f"SawitMod-ios-{VERSION}/PACKAGE.md")
            count += 1

    size_mb = os.path.getsize(zip_path) / 1024 / 1024
    print(f"打包完成：{zip_path}（{count} 个文件，{size_mb:.1f} MB）")

    # 工作流单独放一份（Windows 用户不一定要解压 zip 就能看到）
    wf_dir = os.path.join(os.path.dirname(ROOT), ".github", "workflows")
    if os.path.isdir(wf_dir):
        for name in sorted(os.listdir(wf_dir)):
            if name.endswith((".yml", ".yaml")):
                shutil.copyfile(os.path.join(wf_dir, name),
                                os.path.join(DIST, "github-actions-" + name))

    sums = []
    for name in sorted(os.listdir(DIST)):
        p = os.path.join(DIST, name)
        if os.path.isfile(p) and name != "SHA256SUMS.txt":
            sums.append(f"{sha256(p)}  {name}")
    with open(os.path.join(DIST, "SHA256SUMS.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(sums) + "\n")
    print("校验文件：dist/SHA256SUMS.txt")


if __name__ == "__main__":
    main()
