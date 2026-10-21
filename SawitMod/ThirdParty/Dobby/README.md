# Dobby（外部依赖，不随仓库分发）

把用 Xcode/Theos 编出来的 **iOS arm64** 静态库放到这里，文件名固定：

```
libdobby.a
```

编译方法见 `docs/构建与注入.md` 的「编译 Dobby」一节。

为什么不在仓库里放二进制：
- Dobby 是 LGPL-2.1 的第三方项目，源码/二进制由使用者自行获取更干净
- 静态库必须和最终签名、架构（arm64 / arm64e）匹配
- 换 Xcode 版本后需要重新编译

缺少本文件时 `make` 会在 `before-all` 阶段直接报错并给出路径，而不是让链接器
吐一屏 undefined symbol。
