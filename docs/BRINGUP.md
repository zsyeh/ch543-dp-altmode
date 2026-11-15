# 分阶段上板验证

> 2026-11-15 Board2 更新：实际 MUX 为 CH9445F。实际引脚、仅 D 的 lane 能力、VBUS 禁用条件及 I²C 故障边界见 [BOARD2](BOARD2.md)。本文中的原 GPIO 参考板接线不适用于该板。

当前只有本地协议模拟、PHY 寄存器功能模型和 SDCC 构建记录。本文件是待执行步骤，不是已完成测试报告。需要 CH543D 实物、可编程 PD 对端或分析仪、示波器/逻辑分析仪、具有明确真值表的 MUX 板，以及真正的 DP Source/Sink 视频端。

## 1. 空载与安全输出

先按 [HARDWARE](HARDWARE.md) 核对参考 GPIO 契约，修改为实际板级设计。核对 `PRODUCT_VID/PID`、本板实际支持的 C/D/E 引脚、VBUS_OK 电平范围和供电。初始实验保持外部 MUX 禁用，可不接高速线；不能将参考 GPIO 编码直接套到任意 MUX。

只给 MCU 合适的供电，验证启动后 MUX_EN、AUX_EN 和 HPD_OUT 为低；默认固件不输出 VBUS 控制。测量时钟和 Timer2 的 1 ms tick、Timer0 的 GoodCRC 延时及 Timer1 HPD 脉冲。确认 ISP 下载只写应用区，保留芯片已有引导区和配置。

## 2. 最小 PD / VDM 收发

使用分析仪供给固定 5 V，检查两种插入方向的 CC/Rd、VBUS_OK 和 120 ms 软件去抖。验证 RX DMA 包含 CRC、实际 CRC 字节顺序、有效 SOP 的 GoodCRC 消息 ID，以及错误 CRC 不回复。

捕获 Source_Capabilities → Request → Accept → PS_RDY，确认请求的是第一个固定 5 V PDO且不超过广告电流。先保持 USB UFP，以 USB DFP 对端发送 Discover Identity (`SVID=FF00, SVDM=1, version=0, command=1`)。核对 ACK、ID Header、证书 ID 和 Product VDO，测试重发的 Message ID 不重复执行协议动作。随后发送 Discover SVIDs，确认包含 FF01。

**GoodCRC 延迟必须从实际接收 EOP 到本端发送起点测量。** 软件 Timer0 的名义 50 μs不包含 CRC收尾、ISR、复制和抢占全部时间；PC 模型不会给出这一结果。若不满足目标规范，先检查生成汇编和实测瓶颈，再选择局部汇编或调整驱动。不要通过增加协议重试掩盖电气时序错误。

## 3. 完整 DP 协商与外部硬件

依次验证 Discover Modes、Enter、Status、Configure，并在对端故意返回 NAK/BUSY/超时时确认 MUX 始终安全。有效 Configure 成功后，测量先 MUX 再 AUX 的顺序和 HPD 至少 3 ms 的低电平保护；检查 C/E 四 lane 与 D 两 lane+USB3的真实路由。

分别运行两个默认视频角色镜像；通过对端 DR_SWAP 或应用 API 测试 USB DFP/UFP 两种数据角色。不要以 Power Source/Sink 命名推断视频角色。DP Link Training 和 AUX 事务由视频端完成，CH543D 只选择/隔离这些连接。

## 4. 恢复与边界

捕获连续 HPD IRQ、HPD 低电平、协商中拔线、已配置拔线、Soft Reset、Hard Reset 的 VBUS 下降恢复、重复消息和非法 VDO。检查模式内 DR_SWAP 的恢复、DFP 退出的 Configure=0 → Exit 顺序，以及任何失败后的安全输出。

用 SP 观测和栈水位检查最深协议路径叠加低优先级中断及 USBPD 抢占，核对 [STACK](STACK.md) 的估计。测试记录应包含硬件版本、芯片批次、固件提交、编译器、供电条件、对端型号和原始抓包；单次出图不能证明完整兼容性。

合规验收仍需要完整目标版本规范、USB-IF/VESA CTS及产品所需的 Billboard、电源与线缆能力。当前默认板级 HAL 的电源 Source、CC 持续断开检测和实际 MUX 适配边界见 [STATUS](STATUS.md)。
