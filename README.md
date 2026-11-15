# CH543D USB-C DisplayPort Alt Mode

面向 WCH CH543D 的小型 C 固件工程：内置 PD PHY 负责协商，外部视频芯片和 MUX 承载 DP/AUX 高速信号。支持 DP Source / DP Sink 两种视频角色，并分别管理 USB 数据角色和 PD 供电角色。

**当前是可构建、可在 PC 回归测试的硬件实验版本；尚未在 CH543D 实物、PD 分析仪或真实显示器上验证。** 默认两个镜像都使用 PD Sink 供电，绝不因选择 DP Source 而开启 VBUS。新增基于实际 EPRO 网表的 **Board2 / CH9445F** 适配；默认因原图 VBUS 检测问题禁用 PD attach，详见 [BOARD2](docs/BOARD2.md)。

本地测试包括 72 项通用协议测试 / 27,205 个断言、7 组 PHY 模型，以及真实 CH9445 驱动、I²C 位序、Board2 HPD、八种角色/方向组合的完整协商和故障注入。Board2 的 SDCC 镜像各占 Flash 11,014 B、iRAM 静态分配 191 B、xRAM 204 B，栈区域 64 B；生成汇编估计峰值 32 B。完整结果见 [RESOURCES](docs/RESOURCES.md)。

## 已有功能

- 事件驱动 PD2.0 / SVDM1.0 / SOP 协议子集，固定 5 V 合同；消息 ID、重复过滤、GoodCRC、重试、Soft Reset 与 Hard Reset 恢复。
- FF01 Discover Identity / SVIDs / Modes、Enter、Status、Configure、Attention、Exit；ACK / NAK / BUSY / 超时与安全恢复。
- USB DFP/UFP × DP Source/Sink 四种组合；通用核心支持 C/E 四 lane 与 D 两 lane + USB3 引脚选择，实际 Board2 只声明 D；有效协商完成才使能 MUX/AUX。
- HPD 电平与 IRQ，配置后 HPD 保持低至少 3 ms 的软件防护；真实脉宽和响应延迟仍待实测。
- 实际 CH543 PHY 的 DMA、CRC、GoodCRC 和中断驱动，以及 PC Mock HAL、真实驱动寄存器功能模型。

功能边界见 [STATUS](docs/STATUS.md)，芯片与规范证据见 [AUDIT](docs/AUDIT.md)。不支持有源线缆协商、SOP′/SOP″策略、DPAM2.x/UHBR、PPS、PR_SWAP、VCONN_SWAP、Billboard 或完整 PD 产品认证。

## 构建与本地仿真

Debian / Ubuntu 示例：

```sh
sudo apt-get install gcc make python3 sdcc
make test
make sanitize
make firmware
```

本次实际使用 SDCC 4.5.0，`--model-small --opt-code-size`。`make firmware` 同时生成：

- `build/board2/ch543-dp-source.hex` / `ch543-dp-sink.hex`：CH9445 Board2，默认受限实验镜像，先阅读 [供电约束](docs/BOARD2.md)。
- `build/ch543-dp-source.hex` / `ch543-dp-sink.hex`：原 GPIO 参考板，**不能用于 CH9445 Board2**。
- 同名 `.ihx/.map/.mem/.resources.json/.resources.md`：目标镜像、链接分配和模块统计。

每次成功链接会检查复位、启动 hook 和实际使用的 ISR 向量（参考板 5 个，Board2 4 个），打印 Flash / iRAM / xRAM / 栈分配及生成汇编的栈估计。Flash 链接上限为 14 KiB，保留 CH543 通常的 ISP/数据区。编译器版本变化后请重新检查内存和生成的汇编。

PC 仿真运行的是相同的 C 协议核心。`make test` 包含协议帧/状态断言和真实 PHY 的 SFR 功能模型；`make sanitize` 使用 ASan/UBSan。它们没有模拟 8051 指令时钟或 BMC 电气波形。测试覆盖映射见 [tests/README](tests/README.md)，实际运行记录见 [validation](docs/validation/)。

Keil C51 的存储限定和中断语法提供了适配宏，但本机未安装 Keil，没有实际构建记录或可声称已验证的 Keil 工程。**这些 HEX 不能烧到 AT89C52 使用**：8051 指令集相近不代表 PD 外设、SFR、时钟和存储布局兼容。

## 配置与接入

`include/config.h` 配置产品身份、引脚能力、电流限制和偏好。`DEFAULT_DP_ROLE=1/2` 选择视频角色；运行时仅在未连接时调用 `pd_set_video_role()` 切换，避免已进入模式的角色突然变化。

初始 PD Sink 对应 USB UFP，主循环不会自动强制 DR_SWAP。它响应对端 DFP 的 VDM；应用可在合同建立且未入模式时调用 `pd_request_data_swap()`，或接受对端请求。USB DFP 才发起发现/配置，视频 Source 不自动等于 USB DFP。

`include/hal.h` 定义 PHY、CC方向、VBUS检测、MUX、AUX、HPD及 tick 接口。`include/board.h` 和 `hal/ch543_board.c` 是明确引脚契约的参考板级适配，**不是任何现成开发板或商业 MUX 的通用接线**。上板前按 [HARDWARE](docs/HARDWARE.md) 核对 GPIO、外部 Rd/VBUS 监测、供电与 MUX 真值表。

当前默认不允许真实 PD Power Source；PC 测试覆盖 Source 策略分支，但 Source 电源时序和保护未完成。Board2 始终拉高 PWREN#，不主动供电；默认 `BOARD2_VBUS_VERIFIED=0` 也禁止 attach，硬件修正后才可按 BOARD2 文档启用。

## 工程索引

| 路径 | 内容 |
|---|---|
| `src/pd.c`, `src/dp.c` | PD 链路/最小电源策略与 DP Alt Mode FSM |
| `hal/ch543_phy.c`, `include/ch543_regs.h` | 经官方寄存器核对的目标 PHY 驱动 |
| `hal/ch543_board.c`, `include/board.h` | 参考 GPIO MUX/AUX、HPD、VBUS 与定时器 |
| `hal/ch543_board2.c`, `hal/ch9445.c`, `hal/soft_i2c.c` | 实际 Board2 GPIO、CAP1 HPD、CH9445 事务与软件 I²C |
| [BOARD2](docs/BOARD2.md), `hardware/board2-netmap.json` | 板图审计、四组路由、D-only 限制及必要硬件核对 |
| `hal/mock.c`, `tests/` | PC 仿真和错误注入 |
| [PROTOCOL](docs/PROTOCOL.md) | 状态迁移、角色及恢复语义 |
| [BRINGUP](docs/BRINGUP.md) | 从最小 VDM 收发到真实视频的上板验证步骤 |
| [RESOURCES](docs/RESOURCES.md), [STACK](docs/STACK.md) | 实际资源分配、栈估算与限制 |
| [AUDIT](docs/AUDIT.md), [SOURCES.json](docs/SOURCES.json) | 芯片审计、来源和固定版本哈希 |

资料缓存获取：`python3 tools/fetch_sources.py`，构建不依赖下载 SDK。独立实现采用 MIT 许可，第三方资料边界见 [THIRD_PARTY](docs/THIRD_PARTY.md)。
