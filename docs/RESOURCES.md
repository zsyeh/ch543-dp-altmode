# 当前构建资源：Board2 / CH9445 更新

2026-11-15 截图排查后重新执行干净构建，随后修复 Sink 提前报告 HPD 的问题，普通协议镜像较此前增加 24 B Flash；最新数值如下表。新增独立诊断镜像（不链接 PD）：**Flash 1305 B、iRAM 18 B + 15 B 空洞 + 223 B 栈区域、xRAM 16 B、静态栈估计 16 B**。模块分配：main code97；board2_diag code685/data5/idata1/xdata16；soft_i2c code431/data2/overlay1/bit3。寄存器组、位支撑与运行库计入总量，overlay不可累加。七个测试程序均通过普通运行和 ASan/UBSan；[本次原始日志与报告](validation/diagnostic/)。新文件名包含板型，默认 `make firmware` 不再构建参考板；旧版记录路径保留为历史证据。

2026-11-15，实际 SDCC 4.5.0 #15242。本表包含新增诊断与 Sink HPD 修复；完整模块/内存/栈报告见 [当前验证目录](validation/diagnostic/)，链接日志为 `sink-fix-build-test.log` 与 `sink-fix-guard-rebuild.log`。

| 板型 / 视频镜像 | Flash | iRAM 静态 | iRAM 空洞 | 栈容量 | xRAM | 静态栈估计 |
|---|---:|---:|---:|---:|---:|---:|
| Board2 Source / Sink（各） | 11,038 B | 191 B | 1 B | 64 B | 204 B | 32 B |
| Board2 `VBUS_VERIFIED=1` 编译分支（各） | 11,048 B | 191 B | 1 B | 64 B | 204 B | 32 B |
| 原 GPIO 参考板 Source / Sink（各） | 10,062 B | 187 B | 3 B | 66 B | 190 B | 30 B |

`VBUS_VERIFIED=1` 仅完成编译检查，未确认实物硬件；默认发布仍为0。[该分支构建记录](validation/diagnostic/sink-fix-guard-rebuild.log)。

Board2 Flash 余量 3,298 B（应用上限 14,336 B）；xRAM 余量 52 B。iRAM 布局 191+1+64=256 B，SP 初值 0xBF。静态汇编估计前台16 B + 低优先级中断4 B + PD中断12 B =32 B，估计栈余量32 B；没有实物水位测量。`tools/estimate_stack.py` 检查直接调用、分支路径、PUSH/POP 和中断返回地址，不涵盖 C 运行库初始化、未建模的编译器变化或异常执行。

| Module | code+const | data | idata | xdata | overlay* | bits |
|---|---:|---:|---:|---:|---:|---:|
| wire | 195 | 0 | 0 | 0 | 4 | 0 |
| pd | 3137 | 24 | 67 | 60 | 8 | 0 |
| dp | 3572 | 45 | 0 | 0 | 4 | 2 |
| ch543_phy | 2246 | 12 | 0 | 130 | 0 | 0 |
| ch543_board2 | 606 | 3 | 2 | 6 | 1 | 0 |
| ch9445 | 570 | 2 | 0 | 8 | 2 | 0 |
| soft_i2c | 431 | 2 | 0 | 0 | 1 | 3 |
| main-source | 189 | 0 | 0 | 0 | 0 | 0 |

* Overlay columns overlap and must not be summed. Register banks, bit backing bytes and linker/runtime overhead are included only in totals. Stack capacity is not measured usage; see docs/STACK.md.

Board2 新增 xRAM 为 CH9445 状态8 B和时间扩展6 B，共14 B；HPD 捕获时间戳2 B在idata。软件 I²C 的读回目的指针明确为 idata，避免默认 generic 指针的额外RAM和运行库开销。未使用汇编优化。

v0.2 历史证据（新版本见本文开头的诊断验证目录）：[完整构建/测试日志](validation/board2/build-test.log)、[Board2 Source 内存报告](validation/board2/board2-ch543-dp-source.mem)、[模块报告](validation/board2/board2-ch543-dp-source.resources.json)、[栈估计](validation/board2/board2-ch543-dp-source.stack.json)、[HEX 哈希](validation/board2/firmware-sha256.json)。同目录包含 Sink 和参考板的 map/mem/资源与栈报告。

76 项协议测试 / 27,812 断言、7 组 PHY 模型、CH9445/I²C/Board2 HAL/完整协商模型与网表路由检查全部通过；全部 C 测试的 ASan/UBSan 通过。每次链接输出实际资源及栈估计，并保存 build 下资源历史。默认 Board2 仍因未确认 VBUS 电路而禁止 attach，不能把编译或仿真通过解读为原图可直接出图。

## v0.3 独立 CI 复验

提交 `0e48704` 的 [Actions 43689935808](https://github.com/zsyeh/ch543-dp-altmode/actions/runs/43689935808) 已通过全部测试、ASan/UBSan 与五个 SDCC 目标。CI SDCC 4.2.0：Board2 Source/Sink 各 Flash **11150 B**、iRAM191+1空洞+64栈、xRAM204、栈估计32；诊断 Flash **1253 B**、iRAM18+15空洞+223栈、xRAM16、栈估计16；参考板各 Flash **10155 B**、iRAM187+3空洞+66栈、xRAM190、栈估计30。已下载 CI artifact，核对 HEX 与 IHX 内容及资源报告中的 SHA256。[CI 记录](validation/diagnostic/ci-build-test.log)、[全部模块资源与哈希](validation/diagnostic/ci-resources.json)。发布附件使用本地 SDCC4.5.0 的镜像，数字与上表对应。

## v0.2 的 GitHub 复验（历史）

代码提交 `b429719` 的 [Actions 43658656642](https://github.com/zsyeh/ch543-dp-altmode/actions/runs/43658656642) 全部通过，SDCC 4.2.0+dfsg-1。Board2 Source/Sink 各为 Flash **11,122 B**、iRAM191 B+1 B空洞+64 B栈、xRAM204 B，生成汇编栈估计32 B；参考板各为Flash10,127 B、iRAM187 B+3 B空洞+66 B栈、xRAM190 B，栈估计30 B。两种编译器的数字分别来自实际链接，不能混用镜像哈希。

[v0.2.0 Board2 实验版附件](https://github.com/zsyeh/ch543-dp-altmode/releases/tag/v0.2.0-board2-experimental) 是本地 SDCC4.5.0 的11,014 B受限镜像；CI产物另存在 Actions artifact。附件仍禁止PD attach，需先处理 BOARD2 文档中的真实电路问题。

# 初版参考板资源记录（v0.1.0）

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
