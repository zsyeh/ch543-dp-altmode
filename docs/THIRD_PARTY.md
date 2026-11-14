# 第三方资料与许可边界

本仓库的 C 实现、测试与工具按 MIT 发布。协议核心独立编写；寄存器地址和位定义依据 WCH 手册及 SDK 核对，驱动初始化/收发顺序参考官方例程并针对 SDCC 和中断模型重新实现。Linux 用于核对公开协议字段及行为，没有引入 Linux 源代码或通用协议栈。

`sources/` 是本地审计下载缓存，被 Git 忽略。WCH SDK、芯片手册、Linux 原文件以及 USB-IF/VESA/ST 文档各自保留原有权利；本工程 MIT 许可不适用于这些资料。没有将 SDK 的 Keil 工程、官方源码或第三方 PDF 再分发到仓库。

可用 `python3 tools/fetch_sources.py` 从原站获取审计所用的 WCH/固定 Linux 版本，文件长度和 SHA256 见 [SOURCES.json](SOURCES.json)。WCH 下载 ID 可能被上游替换，哈希不符时工具会停止，不能将新版本当成本次审计版本。

`PRODUCT_VID`/`PRODUCT_PID` 为零占位值，没有借用 WCH 或其他厂商的产品身份。部署产品须配置自己合法使用的身份，并补齐认证和完整规范复核。
