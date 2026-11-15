# CH543D 板级连接与逐步上板

> 2026-11-15 Board2 更新：实际 MUX 为 CH9445F。实际引脚、仅 D 的 lane 能力、VBUS 禁用条件及 I²C 故障边界见 [BOARD2](BOARD2.md)。本文中的原 GPIO 参考板接线不适用于该板。

参考 HAL 面向 **CH543D，电源角色固定 Sink**。选择 DP Source 或 DP Sink 只改变视频方向；芯片不处理视频高速数据。需要外部 DP 发射器/接收器、高速 MUX、AUX/SBU 通路和 HPD 电路。参考引脚来自 CH543D 引脚表及官方 SDK 的 SFR 定义，来源见 [AUDIT.md](AUDIT.md)。这是一份移植连接约定，不是已经制作或验证的原理图。

## 参考控制接口

| 信号 | CH543D GPIO / 封装脚号 | 约定 |
|---|---|---|
| CC1 | P1.0 / 13 | Type-C CC1，使用内置 CC/PD 功能 |
| CC2 | P1.1 / 12 | Type-C CC2，使用内置 CC/PD 功能 |
| VBUS_OK | P1.2 / 20 | 外部 5 V 窗口比较器/电源监控，高=允许协商 |
| MUX_EN | P1.3 / 19 | 高=高速 MUX 允许导通，低=安全隔离 |
| MODE0 | P1.4 / 4 | pin 分配编码 bit0 |
| FLIP | P1.5 / 6 | Type-C 插入方向；高速和 AUX 的方向解码必须一致 |
| AUX_EN | P1.6 / 7 | 高=允许 AUX/SBU 通路，低=隔离 |
| HPD_OUT | P1.7 / 8 | 本地 DP Source 所见的 HPD；上电/安全状态为低 |
| MODE1 | P3.0 / 11 | pin 分配编码 bit1 |
| VIDEO_SOURCE | P3.1 / 10 | 高=本地 DFP_D，低=本地 UFP_D |
| HPD_IN | P3.2 / 5 | 本地 DP Sink 的 HPD 输入，使用 INT0 边沿捕获 |
| 预留 | P3.3 | 当前不分配 |

以上多位控制是**自定义板级接口**，不是某颗商用 MUX 的寄存器或 pin 真值表。`MODE1:MODE0` 定义 C=`01`、D=`10`、E=`11`；只在 `MUX_EN=1` 时有效。实际板必须添加合适的 MUX/解码逻辑，或重写 `dp_mux_set()` 去匹配真实器件。`MUX_SAFE=0` 表示保持隔离，未承诺保留 USB3 通路。

任何控制线和上电默认态都应有硬件约束：MCU 复位或未运行时，MUX_EN/AUX_EN 保持关闭、HPD_OUT 为低，不能依赖 C 初始化完成之后才安全。具体上下拉、电平转换及上电顺序需按所选器件设计。

## VBUS 与 CC

VBUS 不得直接接普通 GPIO。`VBUS_OK` 应来自适当的限压/电平转换/窗口检测电路；不得接一个固定高电平，也不得把检测到 CC 附着视为 VBUS 已有效。检测电路的阈值、容差、抗浪涌和过压保护需由板级设计保证。

参考实现使用 Sink Rd 和 CC 比较器识别方向，不配置 Source Rp，不提供 VCONN，不分配 VBUS 使能 GPIO。`board_source_allowed()` 固定拒绝，`board_vbus_set()` 不会打开电源。若将来支持 Power Source，必须同时实现 Rp、受控供电开关、限流/反灌保护、VBUS 检测与放电以及 Hard Reset 的关断/恢复流程；仅把返回值改成 1 不构成 Source 移植。

`TYPEC_DEBOUNCE_MS` 是策略层附着去抖，不代替 Type-C 电气认证。初次附着用 0.22 V 比较器档位判断默认 Rp；进入 PD 接收后，活跃 CC 改用 BMC 所需的 0.55 V 档位，不能同时依靠这个比较器测量约 0.4 V 的默认 Rp 直流电平。因此参考 HAL **锁存方向，直到 VBUS_OK 失效**。VBUS 检测应位于能及时反映线缆拔出的节点；本地大电容或反向供电使其长期保持高电平会拖延断开识别。硬件允许时，应增加独立 CC 附着监测。

当前仅用于普通互补端口实验；双 CC 同时有效、配件模式、Ra/线缆、DRP 切换与异常连接仍需专门验证。策略层在已识别的 Hard Reset 后提供最多 1000 ms 的 VBUS 低/高恢复保护，临时 VBUS 无效导致方向读数为零时保留连接；这不代表已验证实际电源恢复时序。

## 高速和 AUX 通路

Type-C 的四组高速差分线交给外部 MUX；DP 数据完全绕过 CH543。C/E 使用四条 DP lane；D 使用两条 DP lane并保留 USB3 对。P/N 极性、lane 顺序、插头/插座方向、实际支持的 pin assignment 都必须按连接器、MUX 和 DP 芯片原厂资料逐项核对。不要从 GPIO 编码推断完整 lane 布线。

AUX 为双向差分通道，通过 SBU1/SBU2 按方向路由。HAL 仅控制使能/翻转，不读写 AUX，不代理 EDID、DPCD 或链路训练。AUX 开关需符合选定 DP 电气要求；SBU 不是可直接连接 MCU 的视频控制总线。Type-C 上的 HPD 经 PD VDM 传输，本地 DP 接口 HPD 才由 GPIO/电平电路连接。[ST TA0356 §3](https://www.st.com/resource/en/technical_article/dm00479305-usb-type-c-and-power-delivery-displayport-alternate-mode-stmicroelectronics.pdf)

CH543 的硬件 I2C 功能不能直接当成任意 MUX 的主控制器使用。需要 I2C 主机的 MUX，应另行实现资源受控的软件 I2C 或采用外部控制器；当前提供 GPIO 参考接口。[WCH CH543 数据手册](https://www.wch.cn/downloads/CH543DS1_PDF.html)

## HPD 与定时器占用

| 资源 | 参考 HAL 用途 |
|---|---|
| 系统时钟 | 48 MHz；切换时钟需重新核对所有 timer 装载值与 PHY 时序 |
| Timer0 | 接收后的 GoodCRC 调度，目标延迟 50 µs |
| Timer1 | HPD 输入脉宽识别与输出 IRQ 脉冲 |
| Timer2 | 独立 1 ms 系统 tick |
| INT0 / P3.2 | HPD 双边沿记录；动态切换检测极性 |
| USBPD ISR | RX/TX/Reset 事件、CRC 和 GoodCRC 必要处理 |

参考 HPD 输出 IRQ 目标为 600 µs 低脉冲；首次同时收到 HPD 高与 IRQ 时，先形成约 2 ms 高电平，再产生 IRQ 低脉冲。输入区分约 0.5–1 ms IRQ 与至少 2 ms 的低电平断开，短毛刺不作为断开。INT0 延迟、两个边沿之间的 timer 抢占与极性切换竞争均未经真实波形验证。`dp_hpd_irq_get()` 取出并清除锁存 IRQ，HPD 逻辑状态在 IRQ 期间保持高。

协议核心 1 ms tick 不适合直接生成 HPD IRQ 或保证 GoodCRC 的微秒期限，故使用独立定时器。增加 UART 日志、关闭中断或挪用这些 timer 之前，必须重新测量最坏时延。

## PHY 验证门槛

CH543 PD DMA 地址高字节固定为 0，只能在 xRAM 低 256 B 内工作。工程为 DMA 分配固定区，协议 TX/RX 缓冲与其区分；官方接收长度包含 CRC，发送长度不含 CRC。硬件生成发送 CRC，但没有据手册臆造接收 CRC 正确标志，因此驱动显式核验收到的 CRC 字节。[WCH 手册第18章及官方 EVT](https://www.wch.cn/downloads/CH543EVT_ZIP.html)

驱动在 RX_BYTE 中断中增量积累 CRC，使用 code 区 1024 B 查表；RX_ACT 检查包括 CRC 字节的残数，再启动 GoodCRC 定时。收到 GoodCRC 还要检查版本和对端电源/数据角色，策略层再匹配 Message ID。软件 CRC 最坏开销和 GoodCRC 截止时间仍是核心上板风险。必须用可解码 PD 的分析仪观察最大长度包：CRC 最后字节到 ISR、校验结束、GoodCRC 开始的时差；同时测坏 CRC 不应答、重复包应答但不重复执行、TX_END 后 CC 驱动及时释放、Hard Reset 识别和方向切换。纯 CPU 指令估算不能替代这些波形。

在这些项目通过之前，固件是实验目标镜像。首次接入时应保持 MUX 关闭，使用可控 5 V 供电端与电流限制，从只收包/GoodCRC 开始逐级测试。成功显示视频之后仍需覆盖相反方向、另一视频角色、拔线、复位和协商失败；验收列表见 [STATUS.md](STATUS.md)。
