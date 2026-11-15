# Keil C51 构建与验证

新增的独立 Board2 UART/I²C 诊断使用 `make diagnostic` / SDCC 构建，不在本脚本的四个目标中。用户当前 CH9445 板只能选择 `board2-*`，不能选择 `reference-*`；普通协议固件没有串口日志，见 [诊断说明](DIAGNOSTIC.md)。

需要 Windows PowerShell、Python 3 和安装了 C51 工具集的 Keil 目录。该目录下应有 `C51\BIN\C51.exe`、`A51.exe`、`BL51.exe`、`OH51.exe`。完整链接需要正式 C51 授权。评估版可复现编译和 L250 链接结果。[Keil 下载](https://www.keil.com/demo/eval/c51.htm) · [评估版限制](https://www.keil.com/demo/limits.asp)

在仓库根目录打开 PowerShell，将示例路径换成你的 C51 安装目录。

```powershell
.\tools\build_keil.ps1 -KeilRoot 'C:\Keil_v5'
.\tools\build_keil.ps1 -KeilRoot 'C:\Keil_v5' -Target board2-source
```

缺省构建四个目标。`-Target` 可选择 `board2-source`、`board2-sink`、`reference-source` 或 `reference-sink`。各目标的日志和 `.lst` 文件写入 `build/keil/<target>/`。成功链接后，脚本使用 OH51 转换，并根据 BL51 的 `.m51` 检查复位和中断跳转、启动指令、栈起点及地址范围。检查通过后才写出 `<target>.hex`。

脚本使用 C51 Small 模型，预留 ISP 区，限制应用 Flash 为 `0x0000–0x37FF`，xRAM 为 `0x0000–0x00FF`。CH543 启动汇编在清 RAM 前关闭中断、选用 DPTR0 并关闭自动递增。它清空片内 iRAM 和 xRAM、按链接地址设置 SP，再跳到 C 运行库。

Board2 两个目标固定使用 `BOARD2_VBUS_VERIFIED=0`，禁止 PD attach，PWREN# 保持关闭供电开关。P3.5 过压风险仍须按 [Board2 电路约束](BOARD2.md) 处理。原 GPIO 参考板镜像仅适用于 [参考接线](HARDWARE.md)。

## 2026-11-15 实测

在 Windows 上使用官方 C51 9.61 安装包中的 C51 9.60.7.0、A51 8.2.7.0 和 BL51 6.22.4.0 测试。v0.2 标签的源码在 C51 上报 C141。将中断前置声明限定在 SDCC 后，四个目标的 C 源码均以 0 错误完成编译并生成 OBJ，启动汇编也以 0 错误完成。

| 目标 | BL51 中间报告的代码大小 | xRAM | 完整固件链接 |
|---|---:|---:|---|
| Board2 DP Source | 9,581 B | 204 B | L250，评估版上限 2,048 B |
| Board2 DP Sink | 9,581 B | 204 B | L250，评估版上限 2,048 B |
| GPIO 参考板 DP Source | 8,803 B | 190 B | L250，评估版上限 2,048 B |
| GPIO 参考板 DP Sink | 8,803 B | 190 B | L250，评估版上限 2,048 B |

表中空间占用来自评估版链接器在 L250 前输出的 map。四个完整目标均未生成 OMF 或 HEX。另用省去协议逻辑的小型测试程序验证 A51 → BL51 → OH51 链路及两种板型的向量和启动字节检查。完整固件的 Keil 链接、最终 HEX 检查、栈峰值和实物通信仍待验证。[实测日志摘录](validation/keil/build-test.log)

本地 `make test`、`make sanitize` 和 SDCC 4.2.0 的 `make firmware` 均通过。GitHub Actions 的自动构建使用 SDCC。已有受限 SDCC 固件可从 [v0.2 实验版](https://github.com/zsyeh/ch543-dp-altmode/releases/tag/v0.2.0-board2-experimental) 下载。
