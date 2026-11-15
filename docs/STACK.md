# 当前 Board2 及参考板栈估计

新增 CH9445 后，实际 SDCC 4.5.0 生成汇编由 `tools/estimate_stack.py` 遍历控制流：条件分支均检查，LCALL/ACALL计2 B返回地址，LJMP尾调用不额外加返回地址；递归、未解析调用、间接跳转、直接修改SP或分支栈深不一致均报错。当前Board2共77个函数、参考板69个函数。不是形式化证明，未包括C运行库初始化；下方旧的28 B只适用于初版。

| 当前构建 | 前台峰值 | 低优先级ISR含PC | PD ISR含PC | 估计合计 | 栈容量 / 估计余量 |
|---|---:|---:|---:|---:|---:|
| Board2 两种视频角色 | 16 B | 4 B | 12 B | 32 B | 64 B / 32 B |
| GPIO参考板两种角色 | 14 B | 4 B | 12 B | 30 B | 66 B / 36 B |

Board2 最深路径位于 `main → pd_poll → hal_poll → ch9445_poll → board_i2c_read → put_byte → clock_high → pause_bus`。生成器会对部分边使用尾调用；显式寄存器保存与返回地址共同形成16 B。IRQ仍为PD高优先级bank1、其他低优先级bank2，同级不嵌套；CAP1时间戳放入idata不会占用前台overlay。Source/Sink共用对象，只在main中改变角色常量。

```sh
python3 tools/estimate_stack.py build/board2/target --image build/board2/ch543-dp-source.ihx
```

脚本当前与链接检查一起执行，要求静态估计余量至少16 B；实际最高水位、ISR延迟和异常流量下行为仍需上板观测。当前原始结果见 [Board2](validation/board2/board2-ch543-dp-source.stack.json) 和 [参考板](validation/board2/reference-ch543-dp-source.stack.json)。换工具链后重新生成，不能继承此数字。

# 初版 v0.1.0 栈估计（历史快照）

2026-11-15，检查实际目标构建生成的 `build/target/*.asm` 与 `build/ch543-dp-source.mem`，编译器为 SDCC 4.5.0 #15242，参数为 `-mmcs51 --model-small --std-sdcc11 --opt-code-size`。Source/Sink 镜像共用协议与 HAL 对象，只改变 `main` 的默认视频角色。

本次产物的 SP 初值为 `0xBD`，可用硬件栈为 **0xBE–0xFF，共 66 B**。较早开发构建曾有 70 B、64 B 等不同栈容量，不能把早期数值用于当前镜像。完整 Flash/RAM 数值以每次链接生成的资源报告为准。

## 前台调用路径

对生成汇编按控制流计数：`PUSH/POP` 各增加/减少 1 B；`LCALL/ACALL` 在调用期间增加 2 B 返回地址；跨函数 `LJMP` 是尾调用，不再增加返回地址。检查了当前 65 个工程生成函数，未发现递归、未解析的运行库调用、间接调用或函数内动态改写 SP。分支分别遍历，不能把互斥路径的 PUSH 全部相加。此检查不把工程函数之外的 C 运行库初始化算成主循环路径。

当前主循环路径的估计峰值是 **12 B**：

| 调用点 | 同时占用的栈 |
|---|---:|
| `main → pd_poll`，LCALL | 2 B 返回地址 |
| `pd_poll → receive_message`，LCALL | 再加 2 B |
| `receive_message → dp_receive → response`，生成器使用 LJMP 尾调用 | 再加 0 B |
| `response` 在 Mode VDO 解析中保存 R7/R6，读取 VDO 后继续保存 R5/R4/R3/R2 | 再加 6 B |
| `response → choose_mode`，LCALL | 再加 2 B |
| `choose_mode` 自身 | 无额外 PUSH 或调用 |
| 合计 | **12 B** |

该路径的关键生成序列是：

```asm
push ar7
push ar6
lcall _wire_get32
; 返回地址已弹出，但 R7/R6 仍保存在栈中
push ar5
push ar4
push ar3
push ar2
lcall _choose_mode
```

`response` 的状态校验路径也会保存最多 6 B 寄存器再调用帮助函数。错误恢复的 `incoming → fail → pd_safe → dp_mux_set` 等路径同样纳入检查，未超过上述主循环估计。`wire_*`、寄存器 HAL 以及所检查的算术不引入隐藏的通用指针或长整数运行库调用；若代码或编译器发生变化，必须重新检查。

## 中断叠加

当前优先级配置是 USBPD 高优先级，其余定时器/HPD 中断为低优先级。低优先级 ISR 使用寄存器 bank2，USBPD 使用 bank1；前台使用 bank0。同级中断不互相嵌套，没有 ISR 内重新开放同级嵌套的代码。

| 中断情况 | 栈来源 | 合计 |
|---|---|---:|
| Timer0/1/2 或 INT0 | 硬件压入 PC 2 B；生成序言保存 ACC、PSW 2 B | 4 B |
| USBPD 的 RX_ACT → `crc_update` 最深点 | 硬件 PC 2 B；保存 bits、ACC、B、DPL、DPH、PSW 6 B；调用前保存 R7/R6 2 B；LCALL 返回地址 2 B | 12 B |
| USBPD 抢占一个低优先级 ISR | 上述两者同时保留 | **16 B** |

`crc_update` 使用专用直接寻址存储保存必要临时值，避免 ISR 帮助函数使用与前台冲突的 overlay 局部变量。只计算 C 层函数调用深度会漏掉 ISR 序言、返回地址，以及 RX_ACT 中显式保存 R7/R6 的 2 B。

因此当前产物的主循环加允许中断嵌套的静态估计为：

```text
前台峰值                   12 B
低优先级 ISR + USBPD 抢占   16 B
估计合计                   28 B
链接器提供                 66 B
估计余量                   38 B
```

## 估计的适用范围

**28 B 是针对这份生成汇编的静态估计，不是形式化证明或实测最高水位。** 未用真实 CH543D 测量 SP、ISP 交接、中断风暴、时钟异常或 RAM 破坏下的行为。C 运行库初始化位于 `main` 之前，应单独检查启动汇编；早期启动 hook 本身没有 PUSH 或嵌套调用，清除中断与 XBUS_AUX 后继续初始化。最终镜像的启动 hook 调用和中断向量已有机器码检查，但这不使启动路径成为已上板实测路径。

以下修改会使估计失效：新增 ISR/日志/库调用、改变编译器或优化/内存模型、允许高优先级 ISR 再嵌套、改变寄存器 bank、引入可重入函数或动态栈参数、修改 RAM 布局。Keil C51 不能沿用 SDCC 的 28 B 数值。

上板后应在不覆盖活跃栈的前提下给空闲栈区域填充标记，结合 SP 观测验证最长 VDM、Soft/Hard Reset、连续 HPD IRQ 与 RX_BYTE 高频中断的组合。测试需保留足够余量，不能把“没有立刻复位”当作没有栈溢出。参考 HAL 的时间预算还需独立进行波形测量。

重新检查入口：

```sh
make firmware
cat build/ch543-dp-source.mem
rg -n '^_[[:alnum:]_]+:|push|pop|lcall|acall|ljmp|ajmp' build/target/*.asm
```

当前 RAM/Flash 报告、构建后的向量检查与此栈估计承担不同职责；任一项通过都不替代实际中断与协议时序验证。
