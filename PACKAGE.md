# 打包说明（SawitMod iOS）

这个目录是**可直接开工的发布包**。里面有：

| 文件 | 说明 |
|---|---|
| `SawitMod-ios-v1.1-src.zip` | 完整 iOS 源码工程（Theos 工程 + ImGui + 字库 + 文档 + 自检 + 两个 Actions 工作流 + 发布脚本） |
| `github-actions-build-ios.yml` | 只编 dylib 的工作流（单独放一份，方便直接丢进仓库） |
| `github-actions-build-and-inject.yml` | 编 dylib + 注入你的 IPA 的工作流（同上） |
| `SHA256SUMS.txt` | 上面文件的校验值 |

> **为什么包里没有现成的 `.dylib`**：dylib 必须用 Apple 的 iOS SDK（clang + iPhoneOS SDK）编译，
> 这台 Windows 机器上没有、也装不了（Apple 只授权在 macOS 上构建 iOS 产物）。
> 所以包里给你三条**都能拿到 dylib** 的路：
> **A** GitHub 编 dylib → Windows 注入；**A2** GitHub 直接吃你的 IPA 产出已注入包；
> **B** macOS 一键脚本。dylib 与签名无关，编出来后在 Windows 上照样注入。

---

## 路线 A（无需 Mac）：GitHub Actions 编 dylib → Windows 注入

### A1. 让 GitHub 编出 dylib

最省事的方式是包里的发布脚本（在解压后的包根目录跑）：

```powershell
# 需要「repo」权限的 Personal Access Token（首次建仓要用；仓库已存在可省略）
$env:GITHUB_TOKEN = "ghp_xxx"
powershell -ExecutionPolicy Bypass -File scripts\publish_github.ps1 `
    -Owner <你的GitHub用户名> -Repo sawitmod-ios -Private
```

脚本会：补 `.gitignore`（构建产物不入库）→ `git init/commit` → 用 API 建仓库 →
push（token 走临时 header，不写进 `.git/config`）。

手动也行：

```bash
git init && git add . && git commit -m "SawitMod iOS"
git remote add origin https://github.com/<你>/sawitmod-ios.git
git push -u origin main
```

然后：**Actions → build-ios-dylib → Run workflow**（10–20 分钟，首次要编 Dobby/ImGui），
在 **Artifacts** 下载 `SawitMod-dylib`。

### A2. Windows 上注入并安装（Sideloadly）

1. 下载 [Sideloadly](https://sideloadly.io)。
2. 准备**已解密**的游戏 IPA（App Store 原包是加密的，注入后跑不起来）。
3. Sideloadly → **Advanced Options** → 点 **`+dylib/deb`** → 选 `SawitMod.dylib`。
4. 把 IPA 拖进窗口左上角文档图标。
5. 填 Apple ID → **Start**。
   - 想先得到"已注入 IPA"：签名模式选 **Export**，导出后再用 TrollStore/AltStore 装。
   - 免费 Apple ID 签的 **7 天过期**；TrollStore 设备不过期。

> Sideloadly 的注入入口就是 Advanced Options 里的 `+dylib/deb`：
> <https://shouganai.gitbook.io/ios-stuff/sideloading/tweak-injection/tools/using-sideloadly>

---

## 路线 A2（无需 Mac、也不折腾注入）：GitHub 直接吃 IPA 出已注入包

包里带了第二个工作流 **`build-and-inject`**：编 dylib + 把你的 IPA 解开 + 注入 +
ad-hoc 重签 + 重新打包，直接给你 `SawitMod-injected.ipa`。

**准备 IPA**（选一种来源）：

- **A2-1 走 Release（推荐，支持大文件）**：仓库 → Releases → Draft a new release →
  把 `game.ipa`（已解密）作为附件传上去 → Publish。
- **A2-2 走直链**：任何能 `curl -L` 下载到的 IPA 地址。
- **A2-3 放进仓库**：仅限 <100MB。

**跑工作流**：Actions → **build-and-inject** → Run workflow，填：

| 输入 | 什么时候填 |
|---|---|
| `ipa_release_tag` + `ipa_asset_name` | 走 A2-1（比如 `v1` / `game.ipa`） |
| `ipa_url` | 走 A2-2（直链优先） |
| `ipa_repo_path` | 走 A2-3（如 `input/game.ipa`） |
| `output_name` | 想换输出文件名时填 |

跑完下载 **`injected-ipa`** 这个 artifact：

- 用 **Sideloadly 打开它 → Start**（Sideloadly 会按你的证书重签并安装）
- 或 **TrollStore** 直接装（ad-hoc 签名可装）
- 或 AltStore/SideStore 装

> 这条路的好处：Windows 上不需要装 `insert_dylib`、不需要在 Sideloadly 里勾注入，
> 只要会"喂 IPA、拿 IPA"即可。

---

## 路线 B（有 Mac）：一键脚本

```bash
unzip SawitMod-ios-v1.1-src.zip -d sawitmod && cd sawitmod

export THEOS=~/theos          # Theos 安装见 docs/构建与注入.md
bash scripts/build_all.sh     # 自动编 Dobby + 编出 SawitMod.dylib
```

注入并重签（生成可安装 IPA）：

```bash
./scripts/inject_ipa.sh ~/xdt.ipa .theos/obj/SawitMod.dylib ~/xdt-mod.ipa
```

---

## 路线 C：越狱设备（可选）

已越狱（Dopamine/palera1n 等）可以把 `SawitMod.dylib` 放进 `/var/jb/usr/lib/`，
用 `DYLD_INSERT_LIBRARIES` 或 TweakInject 指向游戏进程。非越狱设备**不要**走这条路。

---

## 装之前/编之前必须知道的

1. **IPA 必须已解密**：`Payload/*.app/<二进制>` 的 cryptid 必须是 0。
2. **签名有效期**：免费 Apple ID 7 天；TrollStore 不受限。
3. **RVA 未标定**：`Core/Offsets-iOS.h` 里 `0x0` 需要按 iOS 包重新找（`docs/移植说明.md` 第 4 节）。
   不标定不会崩，只是"每帧逻辑"类功能不驱动。
4. **素材目录**：自动绘画/乐器/图片素材放 App 的 `Documents/{Drawings,Instrument,Photos}/`；
   `Info.plist` 加 `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace` 后可用文件 App 拖入。
5. **离线目录**：默认 `SAWIT_OFFLINE_MODE=1`，功能列表来自内嵌 dump；
   切在线：`make SAWIT_OFFLINE_MODE=0`（需要卡密登录且服务器可达）。

---

## 包内自检（可选，Windows 可跑）

```powershell
cd sawitmod
powershell -ExecutionPolicy Bypass -File scripts\check_link.ps1   # 全部 C++ 编译+链接
cl /nologo /utf-8 /std:c++17 /EHsc /I .\SawitMod tests\crypto_test.cpp SawitMod\Core\Crypto.cpp
.\crypto_test.exe                                                 # 加密向量 14 项
```

交付时这两项已通过；不能替代 macOS 真机编译，但能挡掉移植期手误。
