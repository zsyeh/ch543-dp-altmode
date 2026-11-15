# PC 协议回归测试

`test_protocol.c` 使用工程同一份 `src/pd.c`、`src/dp.c` 和 `src/wire.c`，链接 `hal/mock.c`。期望报文由测试使用独立的数值常量构造/解析，不调用被测 `wire_*` 函数生成期望值。建立 PD 合同、DR_SWAP、VDM 发现/进入/配置均通过真实的头部与 VDO 字节驱动；测试不直接设置状态机的合同、角色或模式状态来跳过协商。

直接构建：

```sh
cc -std=c99 -Wall -Wextra -Werror -pedantic -Iinclude -Itests \
  src/wire.c src/pd.c src/dp.c hal/mock.c tests/test_protocol.c \
  -o /tmp/ch543-test
/tmp/ch543-test
```

内存/未定义行为检查：

```sh
cc -std=c99 -g -O1 -Wall -Wextra -Werror -pedantic \
  -fsanitize=address,undefined -fno-omit-frame-pointer -Iinclude -Itests \
  src/wire.c src/pd.c src/dp.c hal/mock.c tests/test_protocol.c \
  -o /tmp/ch543-test-san
/tmp/ch543-test-san
```

测试入口最终输出用例数、失败数和断言数；失败返回非零。每个用例独立复位 Mock 与 FSM，报告第一次不满足的字段或状态。

## 验收覆盖对应

| 要求 | 对应测试 | 检查内容 |
|---|---|---|
| 正常进入 DP Alt Mode | `dfp_source`, `ufp_sink` | Identity/SVID/Mode/Enter/Status/Configure 实际头部、对象位置、VDO、FSM 状态 |
| Source/Sink 视频角色 | `dfp_source`, `dfp_sink`, `ufp_source`, `ufp_sink` | USB DFP/UFP × DP Source/Sink 四种组合，独立 PD Sink 电源角色 |
| 不支持 FF01 | `no_svid` | 无 SVID 后失败、安全 MUX；`svid_low_half` 验证低半字 SVID |
| Enter NAK | `enter_nak` | NAK 响应类型、未进入模式、安全 MUX |
| BUSY | `busy_retry`, `busy_exhaustion`, `malformed_busy` | 50 ms 退避、重发完整 VDM、上限、非法 NDO |
| VDM 超时 | `timeout`, `config_timeout`, `wrong_response` | 一般 VDM 30 ms 超时；Enter/Exit 使用 45 ms 模式计时，边界测试在 30/44 ms 保持等待、45 ms 超时；错误 SVID/对象不能结束事务；已 Enter 的超时保留模式成员身份但关闭 MUX |
| 引脚不兼容 | `pin_mismatch`, `reject_configure`, `active_config_nak` | 无交集/多位 Pin/非法 Pin 拒绝；Status Enabled 与 MUX 一致 |
| HPD/IRQ | `hpd_source`, `hpd_sink`, `dfp_sink_status_hpd`, `ufp_source_status_hpd` | HPD 高低、IRQ、Attention 与 Status 发起方限制、IRQ 消费一次 |
| 协商拔线 | `detach`, `detach_active` | 取消事务、清模式/合同、关闭 AUX/MUX/HPD |
| Soft/Hard Reset | `soft_reset`, `soft_reset_dfp`, `hard_reset`, `hard_reset_vbus`, `retry_exhaustion` | Soft Reset 保留模式与 MUX、Message ID 归零、重建合同；Hard Reset 清模式并恢复默认数据角色；VBUS 短时下降保护 |
| 重复/非法消息 | `duplicate`, `illegal_status`, `illegal_header`, `illegal_roles`, `bad_length`, `generated_malformed_frames` | 同 ID IRQ 不重放、非法保留位/角色/长度、1024 个确定性生成帧的缓冲安全 |
| 失败回安全态 | 上述失败用例及 `config_timeout` | AUX 关闭、HPD 低、MUX_SAFE，区分模式成员身份与硬件启用状态 |

其他覆盖：

- `multifunction`, `pin_e`, `plug_mode`, `mode_object_two`：C/D/E 引脚、2 lane + USB3 偏好、插头/插座 Mode 字段映射及第二模式对象。
- `retry`, `retry_exhaustion`：错误 GoodCRC ID 不完成发送；同 Message ID、相同字节重发三次；Soft Reset 超时升级 Hard Reset。
- `power_source`, `source_gate`, `no_power`, `bad_cap`, `cap_current_limit`：5 V / 500 mA 供电合同的两侧；只模拟已明确授权的源供电；没有 VBUS Power Good 不能附着；请求电流不超过对端 PDO。
- `attention_usb`, `ufp_usb_config`, `attention_exit`, `exit_dfp`, `exit_ufp`：USB 配置 VDO=0 保留模式；已配置的 DFP 先发送 Configure=0 并确认 ACK 后再发送 Exit Mode；Exit Mode 清模式；退出过程中 HPD 不被重新拉高。
- `dr_swap_active`：模式内收到 DR_SWAP 触发 Hard Reset；应用请求在模式内不能发起交换。
- `hpd_guard_irq_once`, `hpd_guard_detach`, `hpd_guard_latest_level`：先 MUX 后 AUX，配置后保持 HPD 低 4 个 1 ms tick，补偿 tick 量化后确保至少 3 ms；采用期间最新 HPD；拔线取消延时；IRQ 不重放。
- `hard_reset_delayed_vbus_cycle`, `hard_reset_hold_timeout`：Hard Reset 后 VBUS 延时下降再恢复，掉电阶段 CC 读数暂时为零，以及 1 秒恢复等待上限。
- `ufp_status_disconnected`, `pending_irq_preserved`, `pending_irq_cleared_by_low`：UFP 收到无连接 Status 关闭硬件；等待输出的 IRQ 跨高电平 Status 保留，HPD 低电平清除。
- `phy_tx_end_missing`, `status_busy_preserves_irq`：PHY 发送完成事件缺失的 5 ms 上限；Status 收到 BUSY 后重试保留未成功提交的 IRQ。
- `enter_timeout_boundary`, `exit_timeout_boundary`：Enter/Exit 在 30 ms、44 ms 仍待应答，45 ms 才超时；Exit 超时不等于退出模式，直到 Hard Reset 才清除 entered。
- `tick_wrap`：16 位 tick 回卷下 BUSY 计时。

## Mock 的边界

Mock 将 `PHY_TX_END` 与 `PHY_ACK` 分开；测试显式注入 GoodCRC 完成事件和 Message ID，因此可检查协议层重发与 ID 管理。RX 注入直接位于 HAL 已完成接收/校验之后；本测试**不验证** CH543 PD PHY 寄存器、CRC 计算、BMC 编解码、GoodCRC 发送时延、DMA 中断、CC 电压门限、SOP 令牌、HPD 微秒脉宽及真实高速 MUX 电气行为。

PC 的时间推进为确定性的毫秒事件；不模拟 USB PD 总线冲突和完整物理层调度。不使用 Mock 的通过结果声称 USB-IF/VESA 合规、真实硬件兼容性或 CH543D RAM/Flash 达标。资源占用需以独立目标固件链接结果为准。

## 实际 PHY 驱动的寄存器模型

`make test` 另运行 `tests/test_phy.c`，以 `CH543_PHY_TEST` 编译同一份 `hal/ch543_phy.c`，共 7 组检查。它与协议层 Mock 是两个测试层次：

- 用独立逐位黄金 CRC（`123456789 → CBF43926`）核对全部 256 个表项、合法接收及 80 个单比特错误。
- 验证实际构建的 GoodCRC 字节、64 种合法 ACK 角色/版本/ID 组合与非法 ACK。
- 验证 SOP′/SOP″忽略、接收长度 0–128 及 255、DMA 指针和方向选择。
- 区分正常发送、GoodCRC、Reset 完成事件，并验证接收背压和拔线取消 ACK。

测试明确模拟 FIFO 清除、SFR/位寄存器标志同步及 DMA，不模拟硅片或 8051 指令周期。能证明这些软件分支的功能，不能证明 W0C 寄存器、电气 CRC 顺序和 GoodCRC 实际时限。最终 HEX 还由 `tools/check_vectors.py` 检查 ISR 和早期启动入口机器码。

## CH9445 / Board2 扩展

- `test_protocol.c` 新增四项：延迟提交及待提交 IRQ 保留、MUX 故障、100 ms 超时、取消提交。当前共 72 项；精确断言数以构建日志为准。
- `test_ch9445.c` 执行真实驱动：四种物理路由，17 个事务阶段 NACK，八个读回错误，18 个取消位置，拒绝 C/E，初始残留路由隔离。
- `test_i2c.c` 执行真实软件 I²C：观察逐位输出，验证 0x90/0x91、寄存器/数据、重复 START、读末字节 NACK、各阶段 NACK、STOP 失败、总线卡住时有界返回。
- `test_board2_hal.c` 执行实际 HAL 的 SFR 功能模型：GPIO/PWREN#、默认 VBUS 防护、140,000 次计时回卷、CAP1 的 HPD IRQ 宽度边界、输出 IRQ 与角色方向。
- `test_board2_protocol.c` 把实际协议和 CH9445 驱动连接：USB DFP/UFP × 视频 Source/Sink × CC1/CC2 共八条协商路径；检查报文字段、D-only 能力、C/E NAK、ACK/GoodCRC 前不切换、读回前不拉高 HPD及17个拔线位置。
- Sink 排查新增 `sink_hpd_waits_for_mux`、`sink_hpd_mux_failure`、`sink_hpd_mux_timeout`、`dfp_sink_hpd_waits_for_mux`：显示器先插好时，完整帧中的 HPD/IRQ 在配置/读回前为零，READY 后分别由正确数据角色通知，失败不报高，激活后的 IRQ 保留。当前总计 **76 项 / 27,812 个断言**；真实 CH9445 八路径模型同步覆盖早到的物理 HPD。
- `test_board2_diag.c` 执行独立诊断实际代码：UART 的分频/引脚/非阻塞发送、完整日志、只写 MUX_PDR=1、七个 I²C 事务失败位置、异常读回不报假成功、旧值失效、70 秒 UART 不就绪下时钟回卷和后续周期恢复。该镜像不链接 PD，不可把它的计时器初始化用于协议固件。
- `tools/check_board2_routes.py` 独立从 EPRO 提取的焊盘连接推导路由，不直接使用驱动的路由常量生成期望结果。

以上均为功能模型，外设时序、实际模拟开关导通和真实视频链路未验证。所有新增 C 测试均进入 `make test` 与 `make sanitize`。
