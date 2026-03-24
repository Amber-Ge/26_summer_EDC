# Module 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | MODULE_LAYER_GUIDE |
| 所在层 | `Code/02_Module` |
| 作者 | 姜凯中 |
| 版本 | v1.00 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |

---

## 1. 层定位与边界

Module 层负责把 Driver 的原子能力组合成业务语义接口，是 Task 与 Driver 之间的语义转换层。

Module 层负责：

1. 板级映射注入（`bind_t`）。
2. 运行上下文生命周期管理（`ctx_t`）。
3. 业务语义输出（事件、状态、控制命令）。

Module 层不负责：

1. RTOS 调度与线程同步。
2. HAL 底层调用细节。
3. 跨任务策略编排。

---

## 2. 统一架构规范（ctx 模式）

所有模块统一接口建议：

1. `*_get_default_ctx`
2. `*_ctx_init`
3. `*_ctx_deinit`（若模块提供）
4. `*_bind`
5. `*_unbind`
6. `*_is_bound`
7. 运行期 API（首参数统一为 `ctx`）

统一收益：

1. 生命周期可视化，状态可追踪。
2. 硬件映射集中，换板成本低。
3. Task 层调用风格统一，阅读成本低。

---

## 3. 模块清单与职责

| 模块 | 主要职责 | 下层依赖 |
| --- | --- | --- |
| `mod_led` | LED 通道语义控制 | `drv_gpio` |
| `mod_relay` | 继电器语义控制 | `drv_gpio` |
| `mod_sensor` | 多路循迹状态与权重计算 | `drv_gpio` |
| `mod_key` | 按键映射与事件语义输出 | `drv_key + drv_gpio` |
| `mod_motor` | 双电机执行抽象（方向/PWM/编码器） | `drv_gpio + drv_pwm + drv_encoder` |
| `mod_battery` | 电池电压采样与换算 | `drv_adc` |
| `mod_oled` | OLED 渲染与刷新封装 | I2C/OLED 组件 |
| `mod_vofa` | VOFA 数据发送 | `drv_uart` |
| `mod_k230` | K230 帧接收解析 | `drv_uart` |
| `mod_stepper` | 步进协议封装 | `drv_uart` |
| `mod_uart_guard` | 串口发送仲裁 | UART 相关模块 |

---

## 4. 重点模块深度解耦说明

### 4.1 `mod_motor`（核心模块）

#### 4.1.1 数据所有权

1. `mod_motor_bind_t`：只在绑定阶段作为输入，不在运行期持有原指针。
2. `mod_motor_ctx_t.map[]`：绑定成功后保存硬件映射副本，作为唯一运行配置源。
3. `mod_motor_ctx_t.state[]`：运行态唯一状态缓存（模式、方向、速度、位置、保护标志）。
4. `drv_pwm_ctx_t[]/drv_encoder_ctx_t[]`：由模块内部持有，上层任务不直接访问。

#### 4.1.2 生命周期契约

1. `ctx_init`：仅建立上下文初始状态，可选直接绑定。
2. `bind`：校验并写入映射，不触发硬件动作。
3. `init`：创建并启动 PWM/Encoder 通道，写入安全态（COAST）。
4. `set_mode/set_duty`：运行期命令入口。
5. `tick`：周期读取编码器、更新反馈、释放过零挂起命令。
6. `unbind/deinit`：停止通道并清理状态。

#### 4.1.3 控制链路（任务视角）

1. `DccTask` 在 ON 态调用 `mod_motor_set_duty` 下发目标。
2. `mod_motor_set_duty` 执行限幅、方向解析、过零保护判定。
3. `mod_motor_tick` 读取编码器增量并更新 `current_speed/total_position`。
4. `DccTask` 再通过 `get_speed/get_position` 闭环。

#### 4.1.4 错误降级策略

1. PWM 写失败：通道 `hw_ready=false`，后续拒绝主动驱动。
2. 编码器读取失败：通道降级并清速度输出。
3. 未绑定/未初始化：所有运行期 API 安全返回，不访问底层。

#### 4.1.5 后续扩展方式

1. 新增电机通道：扩展 `MOD_MOTOR_MAX` 与映射表，保持 `ctx` 同构。
2. 新增模式：在 `mode` 枚举与执行分支扩展，不改底层驱动契约。
3. 新增保护：优先在模块内静态函数实现，避免任务层散落保护逻辑。

### 4.2 `mod_key`

1. 通过 `mod_key_hw_cfg_t` 映射“硬件按键 -> 业务事件”。
2. `drv_key` 单实例约束由模块层通过 `s_active_ctx` 管理。
3. Task 层只消费事件，不直接读电平。

### 4.3 `mod_sensor`

1. 逐通道读取 GPIO 状态，输出 `states[]` 与归一化权重。
2. 权重裁剪到 `[-1,1]`，保证上层控制输入边界稳定。
3. 采样逻辑封装在模块，任务层不处理引脚细节。

### 4.4 `mod_led` / `mod_relay`

1. 两者保持同构接口，降低心智负担。
2. `active_level` 解决板级高/低有效差异，任务层只用语义接口。
3. `init` 默认写安全态（LED 全灭、继电器全断开）。

### 4.5 `mod_battery`

1. `update` 做“采样 + 换算 + 缓存”。
2. `get_voltage` 只读缓存，避免重复采样带来阻塞。
3. 换算参数由 `bind` 注入，便于不同分压比板卡复用。

### 4.6 `mod_oled`

1. 显存缓存与 I2C 刷新分离，任务层只组织页面数据。
2. I2C 句柄通过 `OLED_BindI2C` 注入，不写死总线实例。
3. 字模资源与渲染逻辑分离（`mod_oled_data`）。

### 4.7 UART 协议模块组（`mod_vofa/mod_k230/mod_stepper/mod_uart_guard`）

统一架构目标：

1. 协议层只做协议语义；UART 细节统一下沉到 `drv_uart`。
2. 每个协议上下文通过 `bind_t.huart` 绑定一个串口。
3. 通过 `mod_uart_guard` 统一仲裁串口归属，防止跨模块抢占冲突。
4. 发送互斥统一采用 `bind_t.tx_mutex` 注入，避免任务层散落锁逻辑。

关键调用链：

1. `ctx_bind -> mod_uart_guard_claim_ctx(owner+ctx) -> drv_uart_register_rx_callback(user_ctx) -> drv_uart_receive_dma_start_ex`。
2. RX 回调内按事件类型处理（例如 K230/VOFA 忽略 HT），必要时重启 DMA。
3. `ctx_unbind -> drv_uart_receive_dma_stop_ex -> drv_uart_unregister_rx_callback -> mod_uart_guard_release_ctx(owner+ctx)`。
4. `mod_uart_guard` 通过 `owner + claimant(ctx)` 统一拦截“同 owner 不同实例”的占用冲突。

`mod_uart_guard` 约束：

1. 同 owner 重复 claim 会累加 claim-depth。
2. `claim_ctx` 仅允许“同 owner + 同 claimant(ctx)”重入 claim。
3. `release_ctx` 仅在 owner 与 claimant 都匹配时生效。
4. depth 归零才真正释放 owner/claimant。
5. 该机制保证“重复绑定/重复初始化”场景不会误释放他人占用。

维护要点：

1. UART 协议模块禁止直接调用 HAL UART/HAL DMA；必须通过 `drv_uart`。
2. 新增 UART 协议模块时，必须接入 `mod_uart_guard` 和 `tx_mutex` 约束。
3. 协议层回调函数首选 `user_ctx` 模型，避免全局单例指针分发。

---

## 5. 调用链示例

1. `task_key -> mod_key(ctx) -> drv_key + drv_gpio`
2. `task_dcc -> mod_motor(ctx) -> drv_gpio + drv_pwm + drv_encoder`
3. `task_dcc -> mod_sensor(ctx) -> drv_gpio`
4. `task_gpio -> mod_led/mod_relay(ctx) -> drv_gpio`
5. `task_oled -> mod_battery(ctx) -> drv_adc`
6. `task_oled -> mod_oled + mod_oled_data -> I2C`

---

## 6. 维护规则

1. 禁止在 Task 层绕过模块直接调用对应 Driver（如 `drv_pwm_*`）。
2. 绑定参数非法时必须失败返回，且不污染已生效状态。
3. 运行期 API 在未就绪场景必须安全返回。
4. 模块对外公开结构体字段变更时，必须同步更新本说明文档。

---

## 7. 回归检查清单

1. 生命周期回归：`init -> bind -> run -> unbind -> deinit` 是否完整可重复。
2. 异常回归：底层驱动失败是否进入预期降级路径。
3. 边界回归：未绑定、空指针、越界 ID 是否安全返回。
4. 调用链回归：任务层是否仍只通过 Module API 调用。

---

## 8. 扩展模板（建议）

新增模块时建议最小结构：

1. `mod_xxx_bind_t`：板级映射输入。
2. `mod_xxx_ctx_t`：运行上下文。
3. `mod_xxx_ctx_init/bind/unbind/is_bound`。
4. 运行期语义接口（首参统一为 `ctx`）。

---

## 9. 版本记录

### v1.00

1. 建立 Module 层单文档详解。
2. 按 ctx 架构统一生命周期描述。
3. 增补 `mod_motor` 数据所有权、生命周期契约、错误降级与扩展指南。
4. 增补 UART 协议模块统一绑定模型（driver 回调 + guard 仲裁 + 发送互斥）。
