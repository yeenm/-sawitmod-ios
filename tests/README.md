# tests — 本机自检

这两个程序**不参与 iOS 构建**，只在开发机上验证移植质量。

## 1. crypto_test.cpp

用公开测试向量验证 `Core/Crypto.cpp`：

- SHA-256（FIPS 180-2：`abc` / 空串 / 448-bit 消息）
- HMAC-SHA256（RFC 4231 case 1/2）
- MD5（RFC 1321）
- RC4（`Key` / `Plaintext`）
- AES-256-GCM（NIST CAVP：空明文 tag、16 字节密文+tag、解密回环、篡改 tag 必须失败）

```powershell
cl /nologo /utf-8 /std:c++17 /EHsc /I .. tests\crypto_test.cpp ..\SawitMod\Core\Crypto.cpp
crypto_test.exe
```

当前：14/14 通过。

## 2. check_link.ps1（在 ../scripts/ 下）

编译并链接**全部平台无关 C++**（状态/授权/卡密/payload/目录/功能/反射/菜单/ImGui），
用 `stub/` 与 `platform_stubs.cpp` 顶替 iOS 专有头与 ObjC 平台符号。

它能抓到：

- 语法/类型错误（MSVC 与 clang 仍有差异，最终需 macOS 再编一次）
- 跨模块"声明了但没实现"的符号
- 重复定义（例如多个 TU 包含同一个 implementation 宏 —— 移植期真的踩到过
  `STB_IMAGE_IMPLEMENTATION` 被两次展开）

当前：全部通过。

## 3. stub/

| 路径 | 顶替 |
|---|---|
| `stub/mach-o/`、`stub/mach/` | Mach-O / vm_* 头 |
| `stub/sys/`、`stub/unistd.h`、`stub/dirent.h`、`stub/fcntl.h` | POSIX 头 |
| `stub/arpa/`、`stub/netdb.h`、`stub/netinet/` | socket 头（微验卡密） |
| `stub/GLES3/gl3.h`、`stub/OpenGLES/ES3/gl.h` | OpenGL ES（CyloneUI 的纹理函数） |
| `stub/strings.h` | strcasecmp |

这些桩只求"能过编译"，不实现功能；真实行为在 iOS 上由系统库提供。
