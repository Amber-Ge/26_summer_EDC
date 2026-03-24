# Driver 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | DRIVER_LAYER_GUIDE |
| 所在层 | `Code/04_Driver` |
| 作者 | 姜凯中 |
| 版本 | v1.00 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |

---

## 1. 层定位与边界

Driver 层是最小硬件访问封装层，目标是把 HAL 能力整理成稳定、低耦合、可预测的接口。

Driver 层负责：

1. 外设原子操作封装（GPIO、ADC、PWM、Encoder、Key、UART）。
2. 参数校验、状态校验与错误返回。
3. 为 Module 层提供可组合的底层能力。

Driver 层不负责：

1. 业务语义和控制策略。
2. RTOS 任务调度与线程同步。
3. 上层协议流程。

---

## 2. 驱动清单

| 驱动 | 核心能力 | 典型上层调用 |
| --- | --- | --- |
| `drv_gpio` | GPIO 读/写/翻转 | `mod_led/mod_relay/mod_sensor/mod_key/mod_motor` |
| `drv_adc` | ADC 单次采样与平均采样 | `mod_battery` |
| `drv_key` | 按键时序状态机（消抖/单击/双击/长按） | `mod_key` |
| `drv_pwm` | PWM ctx 生命周期与占空比控制 | `mod_motor` |
| `drv_encoder` | 编码器 ctx 生命周期与增量读取 | `mod_motor` |
| `drv_uart` | 串口阻塞/DMA 收发、RX 事件分发、端口索引映射 | `mod_vofa/mod_k230/mod_stepper` |

---

## 3. 统一设计约束

1. Driver 接口禁止业务命名（例如 `drv_xxx_business_action`）。
2. 硬件映射参数由上层注入，Driver 不写死板级信息。
3. 可状态化驱动必须显式生命周期：`ctx_init -> start/use -> stop`。
4. 错误语义统一：参数错误、状态错误、HAL 错误可区分。

---

## 4. 非 UART 驱动深度说明

### 4.1 `drv_gpio`

职责：

1. 提供 `write/read/toggle` 三个原子接口。
2. 屏蔽 `GPIO_PinState`，统一为 `gpio_level_e`。

解耦点：

1. 不关心 LED/继电器/按键业务角色。
2. 不持有任何上下文状态，接口幂等简单。

维护要点：

1. 不在该层添加任何业务含义函数。
2. 所有业务电平映射放在 Module 层 `active_level`。

### 4.2 `drv_adc`

职责：

1. 提供单次原始值采样。
2. 提供多次平均采样。

解耦点：

1. 只输出 ADC 原始计数值，不做电压语义换算。
2. 通道初始化与校准由 Core/HAL 负责。

维护要点：

1. 超时策略统一使用 `DRV_ADC_TIMEOUT_MS`。
2. 平均采样失败必须整体失败，避免输出不可信结果。

### 4.3 `drv_key`

职责：

1. 实现按键状态机与时间阈值判定。
2. 输出统一事件类型，不绑定业务语义。

解耦点：

1. 电平读取通过回调注入，避免硬编码 GPIO。
2. 模块层自行映射到业务事件。

维护要点：

1. 调整阈值只影响驱动时序，不应影响事件语义映射接口。
2. 新增手势类型时，先扩展驱动事件枚举，再扩展模块映射。

### 4.4 `drv_pwm`

职责：

1. 维护单通道 PWM ctx（句柄、通道、限幅、反相、启动状态）。
2. 提供启停和占空比设置。

生命周期契约：

1. `drv_pwm_ctx_init`：写入静态配置并清零比较值。
2. `drv_pwm_start`：启动通道（幂等）。
3. `drv_pwm_set_duty`：仅在 `started=true` 下生效。
4. `drv_pwm_stop`：停止通道并清零输出（幂等）。

维护要点：

1. `duty_max` 与上层控制量必须同量纲。
2. 占空比反相仅在驱动层处理，任务层不重复取反。

### 4.5 `drv_encoder`

职责：

1. 维护编码器 ctx（位宽、方向、启动状态）。
2. 提供启停、清零和增量读取。

生命周期契约：

1. `drv_encoder_ctx_init`：绑定句柄并设定位宽/方向。
2. `drv_encoder_start`：启动并清零计数基线。
3. `drv_encoder_get_delta`：读取后清零的窗口模型。
4. `drv_encoder_stop`：停止并清理运行态。

维护要点：

1. “读后清零”语义不可破坏。
2. 若新增绝对值读取，必须新增接口，不能改现有接口语义。

### 4.6 `drv_uart`

职责：

1. 提供统一端口索引映射 `drv_uart_get_port_index`，避免模块层重复维护 UART 映射表。
2. 提供 DMA 发送/接收状态码接口（`*_ex`）和兼容 `bool` 接口。
3. 提供带 `user_ctx` 的 RX 回调注册接口，实现协议层“同构绑定”。
4. 提供 RX 事件类型（IDLE/TC/HT）与 HT 中断关闭接口，减少模块层 HAL 细节耦合。

解耦点：

1. 驱动只分发“字节 + 事件 + user_ctx”，不做协议帧解析。
2. 协议层可独立决定是否忽略 HT 事件、何时重启 DMA。
3. 同一套驱动接口同时支撑 VOFA/K230/Stepper，不区分业务角色。

维护要点：

1. 端口索引映射必须与板级实际启用 UART 保持一致。
2. `HAL_UARTEx_RxEventCallback` 只能做事件分发，不可写入协议语义。
3. 变更 `drv_uart_status_t` 错误码语义时，必须同步核对所有模块层重试/回滚路径。
4. 旧 `bool` 接口仅用于兼容，新增能力优先使用 `*_ex` 状态码接口。

---

## 5. 与 Module 层接口契约

1. Module 层负责生命周期编排，Driver 只执行单次操作。
2. Driver 返回状态码后，Module 必须决定降级或重试策略。
3. Task 层禁止直接调用 Driver 绕过 Module。

---

## 6. 回归检查清单

1. 非法参数输入是否都能安全返回。
2. `start/stop` 幂等行为是否保持。
3. PWM/Encoder 生命周期是否仍满足 `init -> start -> use -> stop`。
4. Module 层是否仍通过 Driver 接口访问硬件，无跨层绕过。

---

## 7. 扩展建议

1. 新增状态化驱动优先采用 `ctx + status` 设计。
2. 破坏性变更前先补迁移接口，再分阶段替换上层调用。
3. 保留非阻塞与阻塞接口语义清晰边界，避免调用方混用。

---

## 8. 版本记录

### v1.00

1. 建立 Driver 层单文档详解。
2. 完成非 UART 驱动的生命周期契约与解耦边界说明。
3. 增补 `drv_pwm/drv_encoder` 的数据契约和维护规则。
4. 补充 `drv_uart` 统一回调模型（user_ctx + RX 事件）与状态码接口规范。
