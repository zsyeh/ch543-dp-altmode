# 实现范围与验证边界

> 2026-11-15 Board2 更新：实际 MUX 为 CH9445F。实际引脚、仅 D 的 lane 能力、VBUS 禁用条件及 I²C 故障边界见 [BOARD2](BOARD2.md)。本文中的原 GPIO 参考板接线不适用于该板。

> 同日截图排查：已确认用户选择了旧参考板 Sink 镜像，普通固件也没有 UART 日志。现提供 [独立诊断镜像](DIAGNOSTIC.md)，修正默认构建与文件命名。新增第七个 C 测试程序以及诊断目标的实际 SDCC 链接/资源/启动检查均通过；[记录](validation/diagnostic/)。用户已确认测试 Sink、偶尔观察到协商但从未出图。进一步修正“物理 HPD 先高、CH9445 未就绪就通知电脑”的竞态风险，新增四项先失败后通过的回归，最新为 76 项 / 27,812 断言；仍待诊断串口和真实波形反馈。

本仓库目标是可以构建、仿真和逐步上板的 CH543D 工程。**当前没有真实 CH543D、CC 波形、DP 显示器或高速 MUX 的验证结果。** 编译成功与 Mock 通过不能推导成实物互通，更不能推导成 USB-IF/VESA 合规。

## 已有实现

| 模块 | 实现范围 |
|---|---|
| 协议核心 | 静态缓冲、事件驱动、单端口 SOP、PD2.0 非扩展消息、Message ID、GoodCRC 事件、重发及超时 |
| 电源 Sink | 识别固定 5 V 首 PDO、按能力限流请求、Accept/PS_RDY/真实 VBUS 检测后建立合同 |
| 电源 Source | 独立于视频角色的软件策略；目标参考板禁用，不能视为实际供电 Source 支持 |
| 视频角色 | DP Source / DP Sink 分别声明能力；编译默认值和拔线时运行时选择 |
| USB 数据角色 | DFP 发起、UFP 响应；合同建立后且模式未进入时可请求 DR_SWAP |
| SVDM | Identity、SVIDs、Modes、Enter、Status、Configure、Attention、Exit；ACK/NAK/BUSY 与超时；Attention 限于 USB UFP，USB DFP 视频 Sink 通过 Status 更新 HPD |
| DP 配置 | 传统 DP signalling、C/D/E pin 能力交集、4-lane 与 2-lane+USB3 选择 |
| 安全 | 未确认配置时 MUX 保持安全；失败/拔线/Hard Reset 恢复安全；VBUS 驱动由板级能力单独约束 |
| 恢复 | Soft Reset 保留已配置模式；传输恢复失败升级 Hard Reset，清模式并恢复默认数据角色；Hard Reset 的 VBUS 低/高恢复保护有 1000 ms 上限 |
| HAL | CC 方向、PHY、计时、MUX、AUX、HPD、电源有效检测分离；Mock 与目标实现共用协议核心 |

具体字段、状态及恢复顺序见 [PROTOCOL.md](PROTOCOL.md)。Flash/iRAM/xRAM 的实际链接分配及栈区域容量见 [资源报告](RESOURCES.md)；栈使用量只有静态估计，没有实测最高水位。

## 已执行的本地验证

2026-11-15 Board2 版通过 **72 项协议测试、27,205 条断言、7 组 PHY 寄存器模型**，以及 CH9445、软件 I²C、Board2 HAL、完整协商模型和网表检查。六个 C 测试程序均通过 ASan/UBSan。SDCC 构建覆盖两种板型的 Source/Sink 四个目标，各自通过复位、中断向量和早期启动 hook 检查。[Board2 构建测试日志](validation/board2/build-test.log)

寄存器模型检查实际 C 驱动中的 CRC、GoodCRC、事件和缓冲处理，不模拟 CH543 外设的真实内部行为。协议 Mock 与寄存器模型仍属于软件验证，两者均不能测量 CC 波形或 ISR 延迟。静态栈估计见 [STACK.md](STACK.md)；测试覆盖映射见 [tests/README.md](../tests/README.md)。

## 明确没有实现或未完成产品化

1. DP 高速数据、编码、链路训练、DPCD/EDID/AUX 协议；这些由外部 DP Source/Sink 完成，CH543 只控制通路。
2. SOP′/SOP″ 有源线缆发现/模式、VCONN 供电与交换、Cable Reset 策略；PHY 有相应符号能力不等于固件已支持。
3. USB4、Thunderbolt、DPAM 2.x/UHBR、PPS/EPR、FRS、PR_SWAP、完整 DRP 电源切换。
4. 全功能 PD policy engine：全部规范计时、供电 Source Hard Reset 的 VBUS 关闭/放电/恢复序列、所有电源异常和互操作策略。
5. 全部 DP 模式组合：A/B/F、动态视频角色切换、多个本地模式、远端所有 mode/cable 类型。
6. CH9445F 实物 I²C、PCB 引脚极性、信号完整性和硬件故障反馈验证。参考 GPIO 控制需在真实板上适配。
7. 正式 USB VID/PID/认证身份与 USB-IF/VESA CTS。零 VID/PID 仅实验占位。
8. 正式授权下的 Keil 固件链接、最终镜像检查及上板验证。四个目标已通过 C51 源码编译和 A51 启动汇编，评估版链接器在生成完整镜像前触发 2 KiB 限制。见 [Keil 构建和验证记录](KEIL.md)。
9. USB Billboard Device Class 的枚举及失败状态呈现。需要 Billboard 的产品形态须另行实现，不能据本工程的 VDM 应答推断完整 USB 类功能。

## 必须上板验证的项目

| 项目 | 本地仿真能够验证 | 仍需真实硬件证明 |
|---|---|---|
| PHY 接收 | 缓冲边界、字段处理、坏包不进入策略 | CRC 字节顺序、长度计数、RX 标志语义、噪声及超长包行为 |
| GoodCRC | 策略层 ACK/Message ID 逻辑 | 包结束到 GoodCRC 的实际延迟、软件 CRC 最坏耗时、CC 收发切换 |
| 重发 | 重发次数、同 ID/同 payload、耗尽后恢复 | BMC 波形、碰撞、半双工 ISR 与计时器抖动 |
| CC | 方向事件与插拔状态迁移 | Rd/Rp 电平、比较器阈值、两种方向、插拔抖动、线缆类型；活跃时方向锁存依赖 VBUS_OK 掉低识别拔线 |
| MUX/AUX | 不提前启用、失败关断、pin 与方向参数 | 实际 lane/SBU 路由、极性、隔离、切换时间与视频训练 |
| HPD | Status/Attention 字段、延迟应用、非法 IRQ | IRQ 脉宽、去抖、输入边沿锁存、对端接受情况 |
| 资源 | 编译链接 map、静态内存、可检查的栈估算 | 中断嵌套最坏栈深、边界运行与长时间稳定性 |
| Reset | 软件状态、计数器和安全输出 | Hard Reset 电气时序、VBUS 下降/恢复、掉电过程中保持安全 |

## 继续上板的顺序

先保持 MUX 禁用，验证 CC 与 VBUS 检测，再用 PD 分析仪测标准包、CRC、GoodCRC 和重复消息；确认最坏 7-DO 包的应答时间。然后只建立 5 V 合同和 Discover Identity，逐步放开 SVID/Mode/Enter/Status。最后接入 MUX，检查 Configure ACK 前后控制线和 HPD，再接视频设备验证两种视频角色和正反插。Board2 只验证 Pin Assignment D，参考板按其硬件接线验证 C/D/E。

审计证据与来源见 [AUDIT.md](AUDIT.md)；板级连接要求见 [HARDWARE.md](HARDWARE.md)。
