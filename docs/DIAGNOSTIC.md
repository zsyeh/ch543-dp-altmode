# Board2 首次上板诊断

## 截图能够确认什么

2026-11-15，用户通过 iCloud/ch 提供烧录工具、串口、逻辑分析仪、实物和原理图截图，并确认这次测试为 Sink。原图留在本地忽略目录，公开仓库只记录必要结论，不分发照片、设备标识或原 EPRO 文件。

- 烧录界面选择 `build/ch543-dp-sink.hex`，最近一次显示 BIN 长度 10127 B、下载/校验完成。这个路径属于旧 **GPIO 参考板**；长度也与 SDCC 4.2 的该镜像相符。它的 MUX、HPD、VBUS 引脚契约不符合实际 CH9445 Board2。文件名中的 Sink 只选择视频角色，不能选择板型。截图没有固件哈希，不能据此证明具体二进制版本。
- COM36 / 115200 / 8N1 串口空白。现有普通协议固件没有 UART 初始化或日志，因此空白不能用于判断 MCU 是否运行，也不能判断 PD 协商结果。
- DSView 显示 50 s 捕获、50 MHz 采样，三个通道没有信号名称。现有截图缺少探头对应引脚，时间轴也不足以读出 I²C 字节或 PD 包，不能据此判定总线故障。
- 正确 Board2 协议镜像默认 `BOARD2_VBUS_VERIFIED=0`，会禁止 PD attach。改烧板型并不会解除原图 VBUS 检测的硬件阻塞，详情见 [BOARD2](BOARD2.md)。
- 已合并另一台设备的提交 `61bcd70`。它增加 Keil C51 构建与验证脚本；四个目标完成 C51/A51 编译汇编，完整链接被评估版 2 KiB 限制阻止，没有完整 Keil 固件可供烧录。

这次修复默认构建目标、镜像命名，并提供单独的串口/I²C 诊断镜像。尚无实物 PD、AUX 或视频成功记录。

## Sink 无图：已复现并修正的时序问题

用户随后补充“第一版偶尔协商成功、Sink 连不上”，并确认**从未出图**、原 PCB 没问题。本次继续检查固件，没有改 PCB 网表或擅自交换通道。尚缺当前运行镜像的文件名/哈希和实际 PD 包，不能把用户观察直接记为完整 VDM/视频通过。

对照 CH9445DS1 开关矩阵、ST TA0356 Tables 1–4、TI SLLA365 Figure 9，Sink 的两组寄存器映射仍为：

| Type-C 视频 Sink | MUX_PDR 10h | MUX_CTLR 11h | SYS_CTLR0 30h / AUX | SYS_CTLR1 31h | SYS_CTLR2 32h |
|---|---:|---:|---:|---:|---:|
| CC1 | 00 | 00 | 01，交叉 | 80 | 52 |
| CC2 | 00 | 00 | 02，直通 | 10 | A4 |

这是 Pin Assignment D 有效协商之后的目标值，不是开机强制写入值。已扩展网表测试，独立验证 Type-C A8/B8 → CH9445 CSBU/SBU → native DP 15/17 的 AUX 极性。没有发现必须更改这些映射的证据。此前第一次 GPIO 参考板镜像本身不含 CH9445 I²C 配置，可能出现有 PD 交互却无正确高速通路的现象。

发现并复现的另一处固件问题：显示器在协商前已插好、物理 HPD 为高时，`dp_local_status()` 直接报告 HPD 高。Configure ACK 已发送，而 CH9445 的 17 步异步配置/读回尚未完成，电脑就可能开始 AUX 访问。Linux 的 [DP Alt Mode 实现](https://github.com/torvalds/linux/blob/23c62304dde29abe92013d85910d03a77dada35d/drivers/usb/typec/altmodes/displayport.c) 在配置完成回调释放待处理的 HPD；它并不知道这块板在 ACK 后还要完成本地 I²C 事务。因此提前宣告 HPD 会给这段空窗留下机会。**这是代码可复现的竞态风险，不是已经证明的实板唯一故障原因，也不把规范允许的预配置 HPD 状态本身说成非法。**

现在 Sink 只在 `DP_ACTIVE`（CH9445 完整读回确认之后）报告 HPD 高：

1. 模式进入、Status、Configure ACK 和 `DP_MUX_WAIT` 期间，线上 HPD/IRQ=0；本地原始 HPD 输入仍可为高。
2. 读回确认 READY 后，USB UFP 通过 Attention 发送 HPD 高；USB DFP 视频 Sink 通过 Status Update 发送，仍严格区分 USB 数据角色。
3. MUX 失败或超时不发布 HPD 高；配置前的 IRQ 丢弃，实际激活后的 IRQ 正常传递。

四个新增回归在修改前全部失败：[失败日志](validation/diagnostic/sink-hpd-before-fix.log)；修改后全部通过：[通过日志](validation/diagnostic/sink-hpd-after-fix.log)。76 项协议测试共 27,812 个断言通过，真实 CH9445 模型的八条协商路径增加“显示器先插好”的完整报文/状态检查。全套普通运行、ASan/UBSan 和 SDCC 构建记录见 [修复构建日志](validation/diagnostic/sink-fix-build-test.log)。仍需实测 Configure ACK → 最后一次 I²C 读回 → HPD Attention → AUX 访问这条时间线。

## 先烧哪个文件

```sh
make clean
make diagnostic
# build/diagnostic/ch543-board2-diagnostic-uart115200.hex
```

该镜像专供实际 CH543D + CH9445F Board2。**它不建立 PD 合同，不进入 DP 模式，也不会显示视频。** 它关闭 CC/PD 和所有中断，将 P3.4 PWREN# 保持高，HPD 保持输入。仅通过 I²C 写 `MUX_PDR(0x10)=1` 请求全部开关关闭，随后读取寄存器；不会写入开启路由的值。

先只使用本地供电/USB 调试接口（连接板载 hub、CH340 和 CH543 ISP 的接口），断开用于 DP Alt Mode 的 Type-C 口和 DP 线缆。原图 R7 应不装，P3.5 接口问题须先解决再接 Type-C VBUS；固件不会消除过压或 R7 旁路。参照板图确认接口，不用“上/下”或线缆颜色代替连接关系。

WCH ISP 下载并校验成功后让应用运行，打开板载 CH340 对应 COM 口，**115200、8 数据位、无校验、1 停止位、无流控**。日志每约一秒重复，因此可以先烧录再打开串口。串口参数和如下例子是固件设计值及模型测试输出，**不是实物测量记录**：

```text
B2DIAG1 PD=OFF EN#=1 A7=48 W10=1 R10=01 CVR=00 R11=00 R30=00 R31=00 R32=00 P1=A5 P3=36
```

P1/P3 是原始端口采样，实际板上的值可能不同。各字段含义：

| 字段 | 含义 |
|---|---|
| B2DIAG1 | 本诊断协议标识，区别于普通 PD 固件 |
| PD=OFF、EN#=1 | 固件关闭 PD、命令 PWREN# 高；不代表已测得 VBUS 没电或物理隔离 |
| A7=48 | CH9445 的 7-bit I²C 地址，线上的地址字节为 90/91 |
| W10=1 / 0 | 写 0x10=1 的事务成功/失败，包括 ACK 与 STOP；不是最终隔离结论 |
| R10=01 | 成功读回低功耗位为 1；如果是 00，保留异常原值，不报假成功 |
| CVR | 0x08 芯片版本原始值；手册默认 00，不要求某个非零 ID |
| R11、R30、R31、R32 | MUX/路由原始值；诊断不重写它们，不假定必为 00 |
| `--` | 该次读事务失败；旧值无效，不能当成 00 |
| P1、P3 | GPIO 原始采样；尤其 P3.5 高不等于合格的 5V-good，也不是电压读数 |

若仍完全没有日志，先在 MCU P3.1 / U10 pin 10 测 TX 波形，并核对 CH340 COM 口、运行/下载模式、供电和复位。串口有日志但 I²C 失败，才检查 CH9445 电源、ADDR、上拉和 SCL/SDA。如果 W10=1 且 R10=01，证明相应读写事务获得预期数据；仍不证明高速路径、开关真实隔离、PD 或视频有效。

## 逻辑分析仪接法

| 通道建议 | 信号 | 接点 |
|---|---|---|
| 0 | SCL | P1.5 / U10 pin 6 / CH_SCL |
| 1 | SDA | P3.2 / U10 pin 5 / CH_SDA |
| 2 | UART TX | P3.1 / U10 pin 10 / CH_TXD |
| GND | 板地 | GND |

不要接到 P1.6/P1.7 的预留 MCU_SCL/MCU_SDA，它们不是这块板上的 CH9445 控制总线。I²C 使用 7-bit 0x48 解码；观察每秒一次的写 0x10=1、读 0x10/08/11/30/31/32。读为重复 START，最后字节由主机 NACK 再 STOP。50 MHz 采样可继续使用，将时间轴放大到一笔事务的毫秒/微秒范围。UART 按 115200/8N1 解码。通道接点尚需用户确认，以上为后续推荐接法，不是已知截图接法。

## 实现依据和验证边界

- [CH543 官方手册](https://www.wch.cn/downloads/CH543DS1_PDF.html) v1.6 §7.2/8/10/12/13：PCON=87h、SCON=98h、SBUF=99h；PIN_FUNC 的 UART 映射 00 对应 P3.0/P3.1。Timer1 模式 2，T2MOD 的 bTMR_CLK/bT1_CLK=1、SMOD=1、UART_2X=0，TH1=230；48 MHz / 16 / 26 = 115384.6 baud，名义误差 +0.1603%，不含片内振荡器误差。
- [WCH 官方 SDK](https://www.wch.cn/downloads/CH543EVT_ZIP.html) Public/DEBUG.C 的 CfgFsys、mInitSTDIO 和 Public/CH543.H 交叉核对时钟、UART 寄存器和计算方式；独立实现，没有引入 SDK 的 printf 或阻塞发送。
- [CH9445 官方手册](https://www.wch.cn/downloads/CH9445DS1_PDF.html) v1.5 §5.4：CVR=08h、MUX_PDR=10h，MUXPD bit0=1 请求全部开关关闭。仅使用此前已审计的 0x48 地址及寄存器。
- UART 使用 Timer1，而普通协议固件用 Timer1 处理 HPD。因此这是**单独链接的诊断镜像**，不可把这个初始化函数加进 `pd_poll()` 固件。当前 Keil 脚本仍只支持四个协议目标；本诊断镜像使用 SDCC 实际构建。
- 前台非阻塞发送，每次 poll 最多一个 I²C 事务或一个串口字节。Timer2 自由运行 4 MHz，无中断；poll 间隔必须小于 16.384 ms。I²C 等待和位操作有界，UART 即使不就绪也继续更新时钟。
- [诊断测试](../tests/test_board2_diag.c) 执行实际 C 代码，检查引脚方向、UART 时钟配置、完整日志、七个事务的故障、原始异常读回、后续读失败不复用旧值、串口停滞及 16-bit tick 回卷。真实位序沿用已测试的软件 I²C；无模拟的硬件时序或电压结论。

SDCC 4.5.0 实际资源：Flash **1305 B**、iRAM 静态占用 **18 B**、链接空洞 **15 B**、xRAM **16 B**；栈区域 **223 B**、生成汇编估计峰值 **16 B**（无 ISR，未测实物水位）。完整构建和模块报告见 [validation/diagnostic](validation/diagnostic/)。

诊断完成后，用户当前 Sink 路径对应 `ch543-board2-dp-sink-guarded.hex`。它仍会因 guard 阻止 PD attach；先修正并确认 VBUS 检测，才能构建 `-vbus-verified.hex`。随后依次测 5 V 合同、VDM、Configure ACK、CH9445 读回、HPD、AUX/两 lane 视频，不能跳过 PHY/GoodCRC 时序验证。
