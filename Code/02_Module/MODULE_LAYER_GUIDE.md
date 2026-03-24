# Module 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Module Layer Guide |
| 所在层 | `Code/02_Module` |
| 作者 | 姜凯中（工程原作者） / Codex（文档整理与体系化说明） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |
| 目标读者 | 模块开发者、任务层开发者、联调与维护人员 |

---

## 1. 层定位与边界

Module 层是“业务语义抽象层”，作用是把 Driver 的硬件接口组合成可复用的领域接口。

Module 层负责：

1. 硬件映射注入（bind map / ctx bind）。
2. 协议上下文管理（init/bind/unbind/process）。
3. 业务语义转换（例如按键事件语义、循迹权重、电池电压换算）。

Module 层不负责：

1. 任务调度和状态机编排（归 Task 层）。
2. HAL 级原始外设调用细节（归 Driver 层）。
3. 算法底层实现（归 Common 层）。

---

## 2. 模块总览

| 模块 | 主要职责 | 典型调用方 | 主要依赖 |
| --- | --- | --- | --- |
| `mod_led` | LED 逻辑 ID 映射与控制 | `task_gpio`, `task_init` | `drv_gpio` |
| `mod_relay` | 继电器 ID 映射与控制 | `task_gpio`, `task_init` | `drv_gpio` |
| `mod_key` | 按键硬件绑定 + 事件语义映射 | `task_key`, `task_init` | `drv_key`, `drv_gpio` |
| `mod_sensor` | 12 路循迹采样与权重计算 | `task_dcc`, `task_test` | `drv_gpio` |
| `mod_motor` | 双电机执行器抽象（方向/PWM/编码器） | `task_dcc`, `task_stepper` | `drv_pwm`, `drv_encoder`, `drv_gpio` |
| `mod_battery` | ADC 原始值到电压值换算 | `task_oled` | `drv_adc` |
| `mod_oled` | OLED 显存渲染与 I2C 刷新 | `task_oled` | I2C HAL, `mod_oled_data` |
| `mod_oled_data` | 字库与位图资源声明 | `mod_oled` | 无运行期依赖 |
| `mod_uart_guard` | UART 归属仲裁 | `mod_vofa`, `mod_k230`, `mod_stepper` | `drv_uart` 句柄 |
| `mod_vofa` | VOFA 协议收发与命令解析 | `task_init`, `task_stepper`, `task_test` | `drv_uart`, `common_str`, `mod_uart_guard` |
| `mod_k230` | K230 帧接收、校验、解析 | `task_init`, `task_stepper` | `drv_uart`, `common_checksum`, `mod_uart_guard` |
| `mod_stepper` | 步进驱动协议组帧与发送 | `task_stepper` | `drv_uart`, `mod_uart_guard` |

---

## 3. 统一解耦模式

## 3.1 映射注入模式（GPIO 类模块）

适用模块：`mod_led/mod_relay/mod_sensor/mod_motor`

模式特点：

1. 绑定阶段注入硬件资源映射表。
2. 运行期只用逻辑 ID 操作，不传播端口引脚细节。
3. 支持硬件改版时只改绑定表，不改任务代码。

## 3.2 上下文绑定模式（协议类模块）

适用模块：`mod_vofa/mod_k230/mod_stepper`

模式特点：

1. `ctx + bind` 双结构，支持“初始化”和“重绑定”。
2. 资源注入包括：UART、互斥锁、信号量、协议参数。
3. 统一通过 `mod_uart_guard` 避免串口重复占用。

## 3.3 任务层最小依赖原则

1. Task 层只调用模块公开 API，不访问模块静态内部状态。
2. 模块层只暴露必要控制面，隐藏协议拼帧/环形缓冲细节。

---

## 4. 逐模块详解

## 4.1 `mod_led`（LED 执行抽象）

关键接口：

1. `mod_led_bind_map()`
2. `mod_led_Init()`
3. `mod_led_on/off/toggle()`

调用链：

1. `InitTask` 注入映射并初始化。
2. `GpioTask` 根据 DCC 运行态驱动灯态。

扩展建议：

1. 新增 LED 通道时先扩展 `mod_led_id_e`，再扩展映射表和上层状态逻辑。
2. 保持 active_level 可配置，适配高低电平有效板卡差异。

## 4.2 `mod_relay`（继电器执行抽象）

关键接口：

1. `mod_relay_bind_map()`
2. `mod_relay_init()`
3. `mod_relay_on/off/toggle()`

当前用途：

1. `RELAY_LASER`：ON 态激光使能。
2. `RELAY_BUZZER`：按键反馈与 STOP 告警复用。

扩展建议：

1. 新增执行器（风扇、电磁阀）可沿用该模式扩充 ID。
2. 高风险输出建议上电默认 `off` 并在 `init` 强制执行一次。

## 4.3 `mod_key`（板级按键语义化）

关键接口：

1. `mod_key_init()`
2. `mod_key_scan()`

内部策略：

1. 使用 `drv_key` 进行消抖、单击/双击/长按判定。
2. 使用映射表把驱动层事件映射为模块语义事件。

解耦价值：

1. Task 层不关心按键电平和消抖细节，只关心事件语义。

## 4.4 `mod_sensor`（循迹采样抽象）

关键接口：

1. `mod_sensor_bind_map()`
2. `mod_sensor_get_states()`
3. `mod_sensor_get_weight()`

核心逻辑：

1. `line_level` 定义每路“黑线有效电平”。
2. `factor` 定义每路权重。
3. 输出权重限制在 `[-1, 1]`。

扩展建议：

1. 改传感器数量需同步调整 `MOD_SENSOR_CHANNEL_NUM` 与上层数组长度。
2. 新板卡可只换映射表，不改 DCC 算法框架。

## 4.5 `mod_motor`（双电机抽象）

关键接口：

1. `mod_motor_bind_map()`
2. `mod_motor_init()`
3. `mod_motor_set_mode()`
4. `mod_motor_set_duty()`
5. `mod_motor_tick()`
6. `mod_motor_get_speed/position()`

内部机制：

1. 组合了方向引脚、PWM 通道、编码器通道。
2. 内置过零保护，防止反向瞬态冲击。
3. `tick` 负责刷新速度增量与累计位置。

扩展建议：

1. 增加电机通道需同步扩展 `MOD_MOTOR_MAX`、映射、任务控制逻辑。
2. 若引入电流保护，建议在模块层做“执行层安全保护”，任务层只感知结果。

## 4.6 `mod_battery`（电压换算）

关键接口：

1. `mod_battery_bind_adc()`
2. `mod_battery_update()`
3. `mod_battery_get_voltage()`

核心公式：

1. `voltage_pin = raw / ADC_RES * ADC_REF`
2. `voltage_battery = voltage_pin * VOL_RATIO`

维护建议：

1. 更换分压电阻后先改 `MOD_BATTERY_VOL_RATIO`。
2. 显示抖动大时优先调 `MOD_BATTERY_CNT` 平均次数。

## 4.7 `mod_oled` / `mod_oled_data`（显示通道）

`mod_oled` 关键接口：

1. `OLED_BindI2C()`
2. `OLED_Init()`
3. `OLED_Clear()/OLED_Show*/OLED_Update()`

`mod_oled_data` 作用：

1. 提供 ASCII/中文点阵资源与示例位图。

设计特点：

1. 双缓冲思想：先写显存缓存，再统一刷新。
2. I2C 异步发送使用 HAL 回调完成状态推进。

扩展建议：

1. 页面逻辑放在 Task 层，不放在 `mod_oled`。
2. 新增字模统一放 `mod_oled_data`，避免渲染逻辑和资源耦合。

## 4.8 `mod_uart_guard`（串口仲裁）

关键接口：

1. `mod_uart_guard_claim()`
2. `mod_uart_guard_release()`
3. `mod_uart_guard_get_owner()`

解决问题：

1. 多协议模块争用同一 UART 时的冲突检测。
2. 防止“后绑定覆盖前绑定”的隐蔽故障。

维护建议：

1. 任何占用 UART 的新模块都必须接入该守卫。
2. 解绑流程必须调用 `release`，避免资源泄漏。

## 4.9 `mod_vofa`（上位机调试协议）

关键接口：

1. `mod_vofa_ctx_init/bind/unbind/is_bound`
2. `mod_vofa_send_float/int/uint/string_ctx`
3. `mod_vofa_get_command_ctx`

实现要点：

1. 默认上下文 + 兼容旧 API。
2. DMA 收发 + 可选发送互斥锁。
3. 接收回调中解析 `start/stop` 命令并释放绑定信号量。

扩展建议：

1. 新命令只在命令表扩展，不改任务层接口。
2. 新发送类型优先复用 `common_str`。

## 4.10 `mod_k230`（视觉误差协议）

关键接口：

1. `mod_k230_ctx_init/bind/unbind`
2. `mod_k230_get_latest_frame`
3. `mod_k230_add/remove/clear_semaphores`
4. `mod_k230_set_checksum_algo`

实现要点：

1. 固定 12 字节帧解析状态机。
2. DMA -> 环形缓冲 -> 流式拼帧 -> 最新帧输出。
3. 支持解析失败重同步。
4. 关闭 HT 中断，按 IDLE/TC 事件搬运数据。

扩展建议：

1. 增加 CRC 算法可在 `checksum_algo` 枚举与校验分支扩展。
2. 增加帧字段时，先改协议常量索引和 decode 结构，再改上层任务映射。

## 4.11 `mod_stepper`（步进发送协议）

关键接口：

1. `mod_stepper_ctx_init/bind/unbind`
2. `mod_stepper_enable/velocity/position/stop`
3. `mod_stepper_process`

实现要点：

1. TX-only，不做回读解析。
2. `tx_active` + 轮询空闲判定发送完成。
3. 发送超时保护统计失败计数。
4. 每个上下文绑定一个 UART + 一个 driver 地址。

扩展建议：

1. 新协议命令优先通过统一 `_send_frame()` 发送入口扩展。
2. 需要回读时建议新增独立 RX 模块，避免破坏当前发送职责单一性。

---

## 5. 模块调用关系（当前工程）

## 5.1 Task -> Module

1. `task_init`：`mod_led/mod_relay/mod_motor/mod_sensor/mod_key/mod_vofa/mod_k230/mod_stepper`
2. `task_dcc`：`mod_motor/mod_sensor`
3. `task_gpio`：`mod_led/mod_relay`
4. `task_key`：`mod_key`
5. `task_oled`：`mod_battery/mod_oled`
6. `task_stepper`：`mod_k230/mod_stepper/mod_vofa/mod_motor`
7. `task_test`：`mod_vofa/mod_sensor`

## 5.2 Module -> Driver

1. `mod_battery` -> `drv_adc`
2. `mod_motor` -> `drv_pwm + drv_encoder + drv_gpio`
3. `mod_led/mod_relay/mod_sensor/mod_key` -> `drv_gpio`（`mod_key` 同时使用 `drv_key`）
4. `mod_vofa/mod_k230/mod_stepper` -> `drv_uart`

---

## 6. UART 资源分配与冲突策略

当前映射：

1. VOFA: `huart3`
2. K230: `huart4`
3. Stepper X: `huart5`
4. Stepper Y: `huart2`

冲突控制：

1. 绑定时先 `mod_uart_guard_claim`。
2. 解绑时必须 `mod_uart_guard_release`。
3. 绑定失败立即回滚回调和资源登记。

---

## 7. 扩展指南（Module 层）

新增模块推荐模板：

1. 先定义 `*_bind_t` 或 `*_map_item_t`（注入资源）。
2. 再定义 `init/bind/unbind/is_bound` 生命周期接口。
3. 业务接口只暴露必要控制面。
4. 对外返回 `bool` 或明确状态码，不抛隐藏副作用。
5. 在 `task_init` 统一注入与初始化。

新增协议模块建议：

1. 强制接入 `mod_uart_guard`。
2. 优先使用 DMA，必要时用互斥锁序列化发送。
3. 回调函数只做“最小搬运与通知”，重计算放到任务上下文。

---

## 8. 维护规范与排障

维护检查点：

1. 所有模块接口是否保持“先 bind 后 run”约束。
2. 解绑时是否完整释放资源（DMA/回调/guard/互斥锁）。
3. 是否存在跨层反向依赖（模块调用任务层）问题。

常见故障定位：

1. 绑定失败：先查参数合法性，再查 UART guard 占用者。
2. 有数据无解析：查 DMA 回调是否触发、环形缓冲是否推进、校验算法是否匹配。
3. 发送偶发失败：查 tx_mutex 竞争、串口空闲状态、超时恢复计数。
4. 行为不符合预期：查 Task 层是否按周期调用 `process/update/tick`。

---

## 9. 模块层版本演进建议

建议后续按以下原则管理：

1. 任何模块新增 API 时同步更新本文件“逐模块详解”。
2. 资源绑定字段变化必须在文档记录“新增字段语义与默认值”。
3. 若模块职责迁移（上移 Task 或下移 Driver），需在变更记录中明确迁移原因。
