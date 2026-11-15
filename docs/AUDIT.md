# 资料审计（2026-11-15）

> 2026-11-15 Board2 更新：实际 MUX 为 CH9445F。实际引脚、仅 D 的 lane 能力、VBUS 禁用条件及 I²C 故障边界见 [BOARD2](BOARD2.md)。本文中的原 GPIO 参考板接线不适用于该板。

审计先于固件实现。以下是能力边界，不构成 USB-IF/VESA 合规声明。

| 项目 | 结论与原始依据 |
|---|---|
| Structured VDM | 可行。PHY 通过 UPD_DMA_L/UPD_T_LEN 发送软件给定字节，硬件不按消息类型限制 payload，因此可以承载合法 SVDM；这是基于寄存器接口的推论，不是已上板验证。CH543DS1 v1.6 第18.2、18.3节；SDK PD_PHY_TX_INIT。 |
| SOP 类型 | 硬件支持 SOP/SOP′/SOP″；UPD_T_SOP 的4个 K-code 可组合，SDK CH543.H 给出 SOP0=0x00、SOP1=0x50、SOP2=0x44、HardReset=0xA9、CableReset=0x61。接收用 MASK_PD_STAT，Reset 需结合 PIF_RX_RST 区分。工程策略仅处理端口 SOP，不提供有源线缆模式协商。 |
| DMA/FIFO | UPD_DMA_L 高字节固定0，DMA须落在 xRAM 0x0000–0x00FF。UPD_COUNT 接收长度包括4字节CRC；UPD_T_LEN 不包括CRC。UPD_DATA 是单字节缓冲接口，不能当成 TCPC 的大 FIFO。SDK 示例 RX30字节未给CRC尾部/异常超长包留足空间，工程使用128字节DMA区。 |
| CRC | 硬件自动生成 TX CRC/EOP，提供 UPD_CRC32 只读检查窗口。手册未明确给出“接收CRC正确”标志及该窗口的校验残数语义，官方示例也未显式验证CRC。因此本工程用软件CRC32核验DMA中的payload和CRC，绝不臆造CRC_OK位。软件核验和GoodCRC的真实时间预算必须用逻辑分析仪测量。 |
| 中断 | USBPD向量10；RX_ACT/RX_RST/TX_END，直接bit写0清除或寄存器相应位写1清除。bPD_CLR_ALL清标志/FIFO并需软件清零；发送后必须及时清bCC_LVO。 |
| SDK范围 | 官方EVT v1.3，2023-06-03，PD_PHY.c v1.0/2022-04-28。实现Rd、CC方向判断、DMA RX/TX、延时GoodCRC、MsgID及定时重发、收到Source_Capabilities后发送硬编码Request。未实现DP、完整电源策略、完整Reset恢复、分离的视频角色，ISR中还有printf/协议处理。不可直接当成完整PD栈。 |
| 资源 | 16KiB Flash=通常14KiB应用+Boot/EEPROM区；256B iRAM+256B xRAM，不是512B通用RAM。默认链接上限0x3800以保留ISP。全16KiB模式需另行规划下载方式。 |
| C51/SDCC | SDK是Keil C51语法，包含sfr/sbit、xdata及interrupt using。不能原样用SDCC构建。工程提供同一核心的C51/SDCC存储限定宏和最小经核对SFR头；本机实际验证SDCC，未验证Keil。 |
| 硬件限制 | 无DP高速收发器、无DP主链路训练；需要外部视频Source/Sink、Type-C高速/AUX MUX、HPD电平/脉冲电路、电源开关与VBUS检测。CH543 I2C只有Slave，不能假定能用硬件I2C控制MUX。协议不能修复缺少高速布线/电源保护/有源线缆支持。 |

## 来源及取得方式

1. [WCH SDK官方页面](https://www.wch.cn/downloads/CH543EVT_ZIP.html)，官网API返回文件ID387，下载 `https://file.wch.cn/download/file?id=387`。下载与先取得的SDK镜像逐字节SHA256相同；原档保存在本地sources，不随仓库再分发。
2. [WCH手册官方页面](https://www.wch.cn/downloads/CH543DS1_PDF.html)，v1.6，2025-08-12，文件ID384，`https://file.wch.cn/download/file?id=384`。采用中文第18章（纸面64–69页），英文v1E作为交叉核对。
3. [Linux typec_dp.h](https://github.com/torvalds/linux/blob/master/include/linux/usb/typec_dp.h)、[pd_vdo.h](https://github.com/torvalds/linux/blob/master/include/linux/usb/pd_vdo.h)、[displayport.c](https://github.com/torvalds/linux/blob/master/drivers/usb/typec/altmodes/displayport.c)：SVID/VDM及DP VDO位定义、插头/插座pin字段交换、配置及HPD处理。实现独立编写，没有引入Linux栈。
4. [ST TA0356 Rev1](https://www.st.com/resource/en/technical_article/dm00479305-usb-type-c-and-power-delivery-displayport-alternate-mode-stmicroelectronics.pdf)：已通过网页PDF解析读取；原始下载连接在本机超时。第2节明确显式PD合同先于Alt Mode，并区分电源、USB与视频角色；第4–6节用于交叉核对协商。
5. [USB-IF PD资料入口](https://www.usb.org/document-library/usb-power-delivery)：本机直接下载受403限制，不能声称已取得完整规范。[USB-IF/VESA公开介绍入口](https://www.usb.org/document-library/vesa-displayporttm-alternate-mode-usb-cr)、[VESA公开标准获取规则](https://vesa.org/vesa-standards/)：未取得VESA完整规范，不声称规范认证；以公开原厂文档与Linux字段实现保守的DPAM 1.x子集。完整规范复核/CTS是发布产品的剩余工作。
6. [TI工程论坛托管的USB PD3.0原始规范](https://e2e.ti.com/cfs-file/__key/communityserver-discussions-components-files/138/USB_5F00_PD_5F00_R3_5F00_0-V2.0-20190829-_2B00_-ECNs-2020_2D00_02_2D00_07.pdf)：通过公开索引核对Soft Reset不退出已进入模式、Hard Reset恢复默认数据角色和退出模式，以及模式进入/退出独立计时。直接下载仍受限，并未取得完整本地文本。本工程45ms模式定时是参考该范围的原型配置，不能替代PD2.0目标版本完整规范与CTS复核。

已取得文件的长度、SHA256与Linux固定commit见 [SOURCES.json](SOURCES.json)。实际PHY寄存器模型测试直接编译同一驱动，覆盖CRC、GoodCRC、SOP和DMA边界，但不证明芯片电气行为或ISR时序。SDCC最终镜像另检查Reset和5个中断向量、早期启动hook的实际机器码，避免仅有ISR定义却没有入口。

## 保守实现范围

PD2.0（header revision=1）、SVDM1.0（version=0）、单端口、SOP、标准非扩展最多7个DO、固定5V、DPAM传统DP信号（Configure signal=1）、插座C/D/E。C/E为4-lane，D为2-lane+USB3；不支持A/B/F、UHBR/DPAM2.x、有源线缆、VCONN交换、PR_SWAP、FRS或PPS。数据DFP发起Discover/Enter/Status/Configure/Exit，UFP响应；Attention独立按视频HPD来源处理。Power Source不等于DP Source。
