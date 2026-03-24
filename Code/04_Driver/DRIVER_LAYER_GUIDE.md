# Driver 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Driver Layer Guide |
| 所在层 | `Code/04_Driver` |
| 作者 | 姜凯中（工程原作者） / Codex（文档整理与体系化说明） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |
| 读者对象 | 底层驱动开发者、模块维护者、硬件联调人员 |

---

## 1. 层定位与边界

Driver 层是“硬件访问最小封装层”，负责把 HAL 接口整理成稳定、可复用的底层契约。

Driver 层负责：

1. 外设读写与状态判断（GPIO/PWM/UART/ADC/编码器）。
2. 参数合法性保护与基础错误返回。
3. 尽量屏蔽上层对 HAL 状态机细节的直接依赖。

Driver 层不负责：

1. 业务语义（例如“这是激光还是蜂鸣器”）。
2. 任务状态机和跨任务同步。
3. 协议帧解析与控制策略。

---

## 2. 驱动清单总览

| 驱动 | 作用 | 典型上层模块 |
| --- | --- | --- |
| `drv_gpio` | GPIO 读写/翻转统一抽象 | `mod_led/mod_relay/mod_sensor/mod_key/mod_motor` |
| `drv_adc` | ADC 原始采样与平均采样 | `mod_battery` |
| `drv_pwm` | PWM 通道设备化封装 | `mod_motor` |
| `drv_encoder` | 编码器定时器启停与增量读取 | `mod_motor` |
| `drv_key` | 按键状态机（消抖/单击/双击/长按） | `mod_key` |
| `drv_uart` | 阻塞收发、DMA 收发、回调分发 | `mod_vofa/mod_k230/mod_stepper` |

---

## 3. 统一设计原则

1. **最小职责**：驱动只做“能稳定访问硬件”的最小封装。
2. **参数保护**：所有接口都应处理空指针和非法参数。
3. **可复用性**：不把业务概念写入驱动（例如 LED、底盘、视觉）。
4. **状态可判断**：关键发送接口提供“是否忙/是否空闲”判定。

---

## 4. 逐驱动详解

## 4.1 `drv_gpio`

对外接口：

1. `drv_gpio_write`
2. `drv_gpio_read`
3. `drv_gpio_toggle`

核心价值：

1. 统一了逻辑电平枚举 `GPIO_LEVEL_LOW/HIGH`。
2. 上层不再直接处理 HAL 的 `GPIO_PinState` 类型差异。

维护要点：

1. 不要在驱动层增加“设备语义函数”（例如 `led_on`），这些应该放模块层。

---

## 4.2 `drv_adc`

对外接口：

1. `drv_adc_read_raw`
2. `drv_adc_read_raw_avg`

实现特征：

1. 单次读取流程：`Start -> PollForConversion -> GetValue -> Stop`。
2. 平均读取内部复用单次读取接口。

维护要点：

1. 超时值 `DRV_ADC_TIMEOUT_MS` 改动要评估系统实时性。
2. 失败路径必须保证 ADC 被 `Stop`，避免状态残留。

---

## 4.3 `drv_pwm`

对外接口：

1. `drv_pwm_device_init`
2. `drv_pwm_start`
3. `drv_pwm_stop`
4. `drv_pwm_set_duty`
5. `drv_pwm_get_duty_max`

实现特征：

1. 设备对象化，支持多通道实例。
2. 启动前置零、停止后清零，保证安全。
3. 支持 `invert` 占空比反相。

维护要点：

1. `duty_max` 必须与上层控制量定义一致。
2. 新增定时器通道支持时同步更新通道合法性检查。

---

## 4.4 `drv_encoder`

对外接口：

1. `drv_encoder_device_init`
2. `drv_encoder_start/stop/reset`
3. `drv_encoder_get_delta`

实现特征：

1. 支持 16 位/32 位计数器位宽。
2. 每次 `get_delta` 后清零计数器，天然适配周期采样增量模型。
3. 支持 `invert` 方向反转。

维护要点：

1. 调用者需保证固定周期调用，才能正确解释增量为速度。
2. 若要读绝对值，需在模块层累计，而非修改驱动接口语义。

---

## 4.5 `drv_key`

对外接口：

1. `drv_key_init`
2. `drv_key_scan`

实现特征：

1. 完整按键状态机：
   - 空闲
   - 按下消抖
   - 按下保持
   - 释放消抖
   - 双击窗口
   - 第二次按压/释放判定
2. 事件槽模型：每次扫描输出一个 pending 事件。
3. 通过回调读取电平，彻底解耦板级引脚。

维护要点：

1. 扫描周期必须稳定，否则阈值 tick 语义会漂移。
2. 修改状态机时先保持原事件语义兼容，再逐步扩展。

---

## 4.6 `drv_uart`

对外接口：

1. 阻塞发送：`send_byte/send_string/send_buffer_blocking`
2. 阻塞接收：`read_byte_blocking`
3. 异步发送：`is_tx_free/send_dma`
4. 异步接收：`receive_dma_start/stop`
5. 回调注册：`register_callback`

关键机制：

1. 维护固定实例回调表（USART1/2/3/UART4/5/USART6）。
2. 在 `HAL_UARTEx_RxEventCallback` 内按实例分发到模块回调。
3. 上层协议模块可共享同一分发中心而不改 HAL 回调入口。

维护要点：

1. 串口实例映射必须与工程实际外设保持一致。
2. 发送前务必检查 `is_tx_free`，避免并发 DMA 冲突。
3. 若新增 UART 实例，必须同步扩展映射表与回调数组。

---

## 5. Driver 与 Module 映射关系（当前工程）

1. `mod_led/mod_relay/mod_sensor/mod_key/mod_motor` -> `drv_gpio`
2. `mod_battery` -> `drv_adc`
3. `mod_motor` -> `drv_pwm + drv_encoder`
4. `mod_vofa/mod_k230/mod_stepper` -> `drv_uart`
5. `mod_key` -> `drv_key`

这层映射保证了：

1. 业务模块不直接写 HAL。
2. 硬件替换时优先改驱动或绑定参数，不影响任务层。

---

## 6. 并发与实时性注意事项

1. `drv_uart` 本身不做跨模块所有权仲裁，仲裁在 `mod_uart_guard`。
2. DMA 接口是异步语义，上层必须处理“发送忙”场景。
3. `drv_key` 事件判定依赖固定扫描周期，实时抖动会直接影响行为。
4. `drv_encoder_get_delta` 读后清零，跨任务并发读取会互相影响，禁止多处直接读。

---

## 7. 扩展指南（Driver 层）

新增驱动建议流程：

1. 先定义设备对象结构体（如需要多实例）。
2. 再定义统一生命周期接口：`init/start/stop/get`。
3. 参数非法必须快速返回，避免隐式异常。
4. 所有 HAL 错误码在驱动层收敛为可理解的 `bool/状态码`。

新增 UART 类驱动能力建议：

1. 优先保持回调分发表模型，避免多个模块重写 HAL 回调。
2. 新增功能保持对现有接口向后兼容，避免模块层大范围改动。

---

## 8. 维护与回归测试建议

建议最小回归集合：

1. GPIO：读写翻转正确性。
2. ADC：单次与平均采样结果一致性。
3. PWM：0%、50%、100% 占空比输出正确性与反相验证。
4. Encoder：正反转增量方向与溢出边界。
5. Key：单击/双击/长按阈值行为。
6. UART：
   - DMA 发送空闲判定
   - ReceiveToIdle 回调触发
   - 多实例回调分发正确性

---

## 9. 常见故障排查

1. 串口无数据：
   - 先查 `drv_uart_register_callback` 是否成功，再查 DMA 是否启动。
2. 串口偶发忙：
   - 查上层是否在 `is_tx_free` 为 false 时仍强发。
3. 编码器方向反了：
   - 查 `drv_encoder_device_init` 的 `invert` 参数。
4. 按键误判：
   - 查扫描周期是否稳定、阈值 tick 是否合理。
5. PWM 输出异常：
   - 查 `duty_max` 与反相配置是否匹配硬件。

---

## 10. 版本演进建议

1. 驱动接口一旦发布，优先“增量扩展”，避免破坏式改名。
2. HAL 升级后先做驱动层回归，再放开上层联调。
3. 每次驱动新增字段或接口，必须在本文件补充“新增语义和边界”说明。
