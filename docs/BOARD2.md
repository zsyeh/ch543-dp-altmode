# Board2 / CH9445F 实际板级适配

本适配依据用户提供的 `ProDoc_Board2_2026-11-15.epro2`，读取原理图与 PCB 焊盘网表，未接触实物。原文件保存在本机忽略目录，不随公开仓库分发。用于复核的有限网表为 [board2-netmap.json](../hardware/board2-netmap.json)，原文件 SHA256 见 [SOURCES](SOURCES.json)。

**这块板按现有连接只能声明 Pin Assignment D（2-lane DP + USB3）**。通用协议核心仍支持 C/D/E，参考 GPIO 固件也保留它们；Board2 的 Mode VDO 只声明 D，收到 C/E Configure 返回 NAK。此结论来自连接拓扑，不是 CH9445 芯片本身只能支持两条 DP lane。

## GPIO 与地址

| 功能 | CH543D / QFN20 引脚 | EPRO 网络 / 对端 |
|---|---|---|
| CC1 / CC2 | P1.0 / 13、P1.1 / 12 | CH_CC1 / CH_CC2 |
| CH9445 I²C SCL | P1.5 / 6 | CH_SCL → U11 pin 8 |
| CH9445 I²C SDA | P3.2 / 5 | CH_SDA → U11 pin 9 |
| HPD，共享输入/输出 | P1.4 / 4 | R52（10 kΩ）→ HDP1 → DP pin 18 |
| VBUS 检测候选输入 | P3.5 / 16 | R5（10 kΩ）→ VBUS；原电路尚不符合本 HAL 的 5V-good 契约 |
| VBUS 电源开关禁用 | P3.4 / 15 | PWREN# → U6 CH217A EN#；始终输出高 |
| ROLE | P3.3 / 14 | R6 标为不加入 BOM；固件不依赖悬空 strap |
| 预留控制口 | P1.6 / 7、P1.7 / 8 | MCU_SCL / MCU_SDA；保持输入，未实现控制协议 |

U11 CH9445F pin 25 ADDR 经 R70 10 kΩ 下拉，**7-bit 地址 0x48**；线上写/读地址为 0x90 / 0x91。R1/R3 提供 4.7 kΩ 上拉。POL、CT1、CT0 是 **I²C 寄存器字段**，不是这颗芯片上的三个 GPIO 控制脚。CH543 的硬件 I²C 是从机，因此这里在实际 SCL/SDA 引脚上实现软件主机。地址、寄存器与接口依据 [WCH CH9445DS1 v1.5](https://www.wch.cn/downloads/CH9445DS1_PDF.html) §5.1、5.2、5.4；CH543 外设与引脚依据 [CH543DS1](https://www.wch.cn/downloads/CH543DS1_PDF.html) §9、10、12、15。

## 高速路径推导

EPRO 的 RX1/RX2 网络名不能替代 Type-C 焊盘号：PORT1 **A11 → U11 CRX2P**，**B11 → CRX1P**；A2/B2 分别接 CTX1P/CTX2P。U11 LNA/LNB 接 SRC_DP0/1，LNC/LND 接 SNK_DP0/1。

普通 DP receptacle 作为 Source 与 Sink 时，逻辑 lane 与正负极性定义不同。由 [TI SLLA365 Figure 9](https://www.ti.com/lit/an/slla365/slla365.pdf) 对照原图可知：本板 native DP 输入的 ML0+/ML1+ 在 pin 12/9，接 LNA+/LNB+；native DP 输出的 ML0+/ML1+ 在 pin 1/4，接 LNC+/LND+。不能因原理图符号统一写成 DP0～DP3，就把其中一半误判为接反。

结合 [ST TA0356 Tables 1–4](https://www.st.com/resource/en/technical_article/dm00479305-usb-type-c-and-power-delivery-displayport-alternate-mode-stmicroelectronics.pdf) 的 Type-C D 路由和 WCH 的开关矩阵，得到以下板级寄存器值。视频角色均以 **Type-C 端口** 为准；CC1=正常方向，CC2=翻转方向。

| Type-C 视频角色 | CC | native DP 使用通道 | SYS_CTLR1 0x31 | SYS_CTLR2 0x32 | SYS_CTLR0 0x30 / AUX |
|---|---|---|---:|---:|---:|
| Source，native DP 输入 | CC1 | LNA/LNB | 0xA1 | 0x40 | 0x02，直通 |
| Source，native DP 输入 | CC2 | LNA/LNB | 0x58 | 0x20 | 0x01，交叉 |
| Sink，native DP 输出 | CC1 | LNC/LND | 0x80 | 0x52 | 0x01，交叉 |
| Sink，native DP 输出 | CC2 | LNC/LND | 0x10 | 0xA4 | 0x02，直通 |

这些值同时接通 D 的剩余 USB3 TX/RX 通道。PORT3 的 TX/RX 网络含交叉与正负标签交换，原图标注适用于 USB3 Device；本固件没有 USB3 数据角色切换硬件，**不能把 PD DR_SWAP 等同于 PORT3 自动变成任意 Host/Device 接口**。USB3 枚举与线缆连接尚未实测。

CH9445 的矩阵并非任意 4×4 crossbar：LNA/LND 只可选两组 CRX，LNB/LNC 只可选两组 CTX。当前 native DP 的四条 lane 无法全部匹配 C/E 的目标对；仅改 POL/CTx 或固件不能补足。`tools/check_board2_routes.py` 从提交的实际焊盘网表连接电容/0Ω桥，推导四组 D 值，并检查 C/E 无可行完整路径。它不验证 PCB 是否已实际布通、阻抗、损耗或焊接。

## 配置事务与故障语义

成功的 Configure ACK（UFP 还需该 ACK 的 GoodCRC）之后，状态进入 `DP_MUX_WAIT`。每次 `hal_poll()` 最多一个 I²C 寄存器事务，事务内只有有界位操作，PD 中断持续开放。

1. 写 0x10=1 关闭全部开关，0x11=0 退出预设模式。
2. 写 0x30=0，0x31=0，0x32=0 清除旧 AUX/通道；读回 0x11、0x30、0x31、0x32 确认全部为 0 后，才写 0x10=0 恢复可控状态，避免假定低功耗状态下的写操作一定生效。
3. 写目标 0x31、0x32；后者将两个寄存器同时提交。随后写目标 0x30 接通 AUX。
4. 分别读回 0x31、0x32、0x30、0x10；全部一致才报告 READY，进入 `DP_ACTIVE`。Source 再保持 HPD 低 4 个毫秒 tick。

拔线、Hard Reset、Exit 或取消配置，会在当次协议处理后将尚未完成的路由替换为关闭操作。普通 SAFE 操作写 0x10=1 并读回；重复 SAFE 不会让关闭操作永远重启。

任何 NACK、SCL 无法拉高、STOP 失败或读回不符均锁存 MUX_FAULT。固件最多额外尝试三次 0x10=1，并压低 Source HPD；需要复位后重试。**若 I²C 总线失效，不能声称物理 MUX 已隔离**：此板没有 MCU 可控的 MUX RESET/供电切断脚。硬件强制隔离须增加独立断电/禁用路径。100 ms 配置超时也会恢复协议安全状态。当前不实现多主机仲裁、I²C 九脉冲总线修复或在线 MUX 掉电监测。

## HPD 与时间基准

P3.2 已是 SDA，不能再作 INT0。Board2 使用 P1.4 的 **Timer2 CAP1** 双边沿捕获，4 MHz 时基，捕获时间戳差 2000～4000 counts 对应 0.5～1 ms HPD IRQ；Timer1 区分持续 2 ms 的低电平，并在 Source 模式生成名义 600 µs IRQ。Source 输出与 Sink 输入共用 P1.4，未连接时变更视频角色会立即调整 GPIO 方向。根据 CH543DS1 §12.5.2，CAP1 模式下 TF2 成为捕获标志，不能继续把它当 1 ms 溢出中断。

Timer2 自由运行计数器由前台扩展成毫秒，**`pd_poll()` 调用间隔必须小于 16 ms**，否则可能漏掉一次 16.384 ms 回卷。现有前台没有睡眠/等待操作；新增日志、Flash 擦写、调试暂停会破坏此条件。捕获寄存器不是队列，ISR 延迟过长仍会丢边沿。真实中断延迟、软件 I²C 频率、GoodCRC 时序和 HPD 脉宽均未上板测量。

## 原图的供电与装配约束

**默认 Board2 镜像是受限实验镜像：`BOARD2_VBUS_VERIFIED=0`，不允许 PD attach。** 这不是“协商失败”，而是原 VBUS 检测尚未满足驱动需要的硬件条件。

EPRO 把 U10 VDD12（pin 1）和 VDD3（pin 3）都接 3.3 V，而 P3.5 仅通过 R5=10 kΩ 接 VBUS。CH543DS1 §19.1 给出的 P3.5 输入绝对上限为 **VDD12+0.4 V**；§10 的输入保护回到 VDD12。串联电阻没有构成分压或 5 V 窗口检测器，不能据此声称该接法允许 5 V 输入。需要修正电平接口，并提供经确认的 5V-good 信号到 P3.5；单纯给一个编译宏置 1 不会修复电路。

R7 是 VBUS↔本地 +5 V 的 0Ω，原理图 `Add into BOM=no`。若实物焊上，它绕过 U6，固件拉高 PWREN# 也不能切断这条路径。R6、AUX 偏置 R2/R4/R71/R72 同样不加入 BOM；这表示设计意图，不证明实物未焊。AUX 现有两端的 AC 耦合、偏置和实际补焊值须一起核对，不能把四个相反方向的偏置电阻全部装上。固定偏置不会随着 `pd_set_video_role()` 自动换向；本固件只控制 SBU 路由。

两种视频镜像均保持 **PD Power Sink**；要求 Type-C 对端能供电并建立相容数据角色。纯 Sink 显示器与本固件不会因视频 Source 角色而自动得到 VBUS。U6 的 Source 供电流程、限流和反灌保护尚未验证，不在此版启用。原生 DP 输入端的 GPU 也无法直接获知 Type-C 仅协商了两条 lane：须确认远端 DPCD/源端能限制或回退到 2 lanes；本工程不代理/改写 AUX DPCD。

## 构建与上板顺序

```sh
make test sanitize firmware
# 默认 Board2 产物；先只核对 I2C、关闭状态、GPIO、计时，不会 PD attach
ls build/board2/*.hex

# 只有修正并验证 5V-good 电路后，才使用此配置；不会开启 VBUS Source
make board2 BOARD2_VBUS_VERIFIED=1
```

构建会跟踪该参数变化并重编译，避免误用旧对象。恢复默认用 `make board2 BOARD2_VBUS_VERIFIED=0`。`build/*.hex` 是旧参考 GPIO 板布局，**不可用于本板**；Board2 文件位于 `build/board2/`。

先测本地供电/VDD12与 P3.5 电压，确认 R7 装配和 PWREN# 高电平；再读写 CH9445 地址 0x48 验证隔离/读回；之后用 PD 分析仪验证 GoodCRC/VDM；最后检查四种路由的 AUX 和两 lane link training。ISP 使用 P1.4/P1.6/P1.7，须避免 HPD 或外接 MCU 干扰下载。不要把 PC 功能模型成功作为此顺序中任何实物步骤的完成记录。
