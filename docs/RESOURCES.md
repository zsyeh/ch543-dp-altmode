# 实际构建资源报告

2026-11-15，SDCC 4.5.0 #15242，`--model-small --opt-code-size`。两个镜像共用协议与HAL对象，只改变默认视频角色；不是两个删减后的单角色栈。

| 镜像 | Flash | iRAM 静态分配 | iRAM 布局空洞 | 栈区域容量 | xRAM |
|---|---:|---:|---:|---:|---:|
| DP Source | 9,913 B | 187 B | 3 B | 66 B | 190 B |
| DP Sink | 9,913 B | 187 B | 3 B | 66 B | 190 B |

Flash 链接限制 14,336 B，剩余 4,423 B，保留通常的 ISP/EEPROM 区。芯片 RAM 是256 B iRAM +256 B xRAM；iRAM 的187 B包含寄存器组、位变量支撑字节、数据和overlay实际分配，不含66 B硬件栈区域。布局为187+3+66=256 B，不能把空洞当成连续栈空间。xRAM余量66 B。

栈初始SP=0xBD，区域0xBE–0xFF。生成汇编静态估计前台12 B、最大中断叠加16 B，合计28 B，估计余量38 B；详见 [STACK](STACK.md)。这是静态估计，尚无实物栈水位。每次链接会生成真实占用，不能用主机GCC的结构体尺寸代替8051结果。

## 模块分配（字节，bits列除外）

| Module | code+const | data | idata | xdata | overlay* | bits |
|---|---:|---:|---:|---:|---:|---:|
| wire | 195 | 0 | 0 | 0 | 4 | 0 |
| pd | 3123 | 24 | 67 | 60 | 8 | 0 |
| dp | 3443 | 45 | 0 | 0 | 4 | 2 |
| ch543_phy | 2246 | 12 | 0 | 130 | 0 | 0 |
| ch543_board | 625 | 5 | 0 | 0 | 2 | 0 |
| main-source | 189 | 0 | 0 | 0 | 0 | 0 |

`code+const` 是各模块CSEG/CONST/HOME的分配；总计加上链接器和C运行库的启动开销才得到完整镜像。overlay列互相重叠，不能求和。PD的idata 67 B =上下文65 B +当前tick 2 B；xdata 60 B = RX30 + TX30。PHY xdata130 B = DMA128 + GoodCRC2，CRC表为1024 B CODE而非RAM。

## 验证证据

- [完整命令输出](validation/build-test.log)：68项协议测试、26,564断言，7组真实PHY寄存器模型，GCC ASan/UBSan，两次目标链接及向量/启动检查。
- [Source .mem](validation/ch543-dp-source.mem)、[Sink .mem](validation/ch543-dp-sink.mem)：SDCC原始内存报告。
- [Source .map](validation/ch543-dp-source.map)、[Sink .map](validation/ch543-dp-sink.map)：原始链接映射。
- [镜像长度和SHA256](validation/firmware-sha256.json)：对应本次本地构建产物。

早期开发构建出现9518/9904 B等不同结果，后来修复了缺失中断向量、增加启动检查并调整协议逻辑；本报告只描述以上最终快照。`make firmware` 在每次成功链接后打印各模块占用，并在build中追加resource-history.jsonl。

本次没有真实CH543D或Keil C51测试。iRAM/xRAM/Flash分配满足链接约束，不等于已经验证所有时序、栈最高水位或USB/DP兼容性。

## GitHub 独立环境复验

提交 `d1cc95c` 的 [GitHub Actions 运行](https://github.com/zsyeh/ch543-dp-altmode/actions/runs/43655484284) 已成功，使用发行版 SDCC 4.2.0+dfsg-1。同样通过68项/26,564断言、7组PHY模型、ASan/UBSan、两个固件的向量与启动检查。

两个镜像在该编译器下各占 **9,998 B Flash、187 B iRAM静态分配、3 B iRAM空洞、66 B栈区域、190 B xRAM**。模块code+const分别为wire194、pd3161、dp3515、PHY2277、board637、main122 B；各模块RAM分配与上表相同。4.2与4.5生成代码不同，4.5的28 B栈使用估计不能自动套用到4.2，CI只检查栈区域容量，没有实测最高水位。

[实验版附件](https://github.com/zsyeh/ch543-dp-altmode/releases/tag/v0.1.0-experimental) 发布的是本地SDCC 4.5.0的9,913 B镜像；CI的4.2.0构建作为Actions artifact单独保存。
