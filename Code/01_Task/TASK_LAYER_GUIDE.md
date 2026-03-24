# Task 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Task Layer Guide |
| 所在层 | `Code/01_Task` |
| 作者 | 姜凯中（工程原作者） / Codex（文档整理与体系化说明） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |
| 读者对象 | 新接手开发者、联调工程师、代码评审人员 |

---

## 1. 层定位与边界

Task 层是工程的“业务编排层”。它不直接操作寄存器，也不直接写 HAL 细节，而是：

1. 调度 Module 层接口形成业务闭环。
2. 管理任务状态机与跨任务同步对象（信号量/互斥锁）。
3. 承担“谁在什么时候调用哪个模块”的时序责任。

Task 层**不负责**：

1. 具体硬件读写细节（归 Driver/Module）。
2. 协议帧格式细节（归 Module）。
3. 算法基础实现（归 Common/PID）。

---

## 2. 启动路径与初始化时序

系统启动关键链路如下：

1. `main.c` 完成 HAL 与外设初始化（GPIO/DMA/UART/TIM/I2C/ADC）。
2. `osKernelInitialize()` 后进入 `MX_FREERTOS_Init()`。
3. `Core/Src/freertos.c` 创建所有任务与同步对象。
4. `InitTask` 执行系统装配：
   - 绑定 LED/继电器/电机/传感器硬件映射。
   - 初始化模块。
   - 绑定 VOFA、K230、Stepper 协议通道。
   - 释放 `Sem_InitHandle` 并自删除。
5. 其它任务调用 `task_wait_init_done()` 通过初始化闸门后进入主循环。

设计价值：

1. 避免多任务重复初始化硬件。
2. 保证“先装配后运行”。
3. 初始化失败不会拖垮整个调度器，任务可按绑定状态做降级。

---

## 3. 任务总览（与 `freertos.c` 一致）

| 任务名 | 入口函数 | 栈大小（字节） | 优先级 | 主要职责 |
| --- | --- | ---: | --- | --- |
| `defaultTask` | `StartDefaultTask` | 512 | `osPriorityLow` | 空闲保底任务，保障调度器常驻 |
| `GpioTask` | `StartGpioTask` | 512 | `osPriorityLow` | LED/蜂鸣器/激光继电器反馈逻辑 |
| `KeyTask` | `StartKeyTask` | 512 | `osPriorityLow` | 按键扫描并分发控制信号量 |
| `OledTask` | `StartOledTask` | 512 | `osPriorityLow` | 电压与模式状态显示 |
| `TestTask` | `StartTestTask` | 512 | `osPriorityHigh` | 调试预留任务 |
| `StepperTask` | `StartStepperTask` | 4096 | `osPriorityRealtime` | 视觉误差闭环控制双轴步进 |
| `DccTask` | `StartDccTask` | 2048 | `osPriorityRealtime` | 底盘 DCC 模式状态机与 PID 控制 |
| `InitTask` | `StartInitTask` | 1024 | `osPriorityRealtime7` | 一次性装配与初始化闸门释放 |

---

## 4. 同步资源与职责分配

| 资源名 | 类型 | 生产者 | 消费者 | 作用 |
| --- | --- | --- | --- | --- |
| `Sem_InitHandle` | 二值信号量 | `InitTask` | 所有业务任务 | 初始化闸门 |
| `Sem_RedLEDHandle` | 二值信号量 | `KeyTask` | `GpioTask` | 任意按键反馈（黄灯+短鸣） |
| `Sem_DccHandle` | 二值信号量 | `KeyTask` | `DccTask` | KEY2 单击：DCC 全重置 |
| `Sem_TaskChangeHandle` | 二值信号量 | `KeyTask` | `DccTask` | KEY3 单击：模式切换 |
| `Sem_ReadyToggleHandle` | 二值信号量 | `KeyTask` | `DccTask` | KEY3 双击：OFF/PREPARE/ON 切换 |
| `PcMutexHandle` | 互斥锁 | `freertos.c` 创建 | VOFA/K230/Stepper 发送路径 | 共享串口发送互斥 |

详细任务资源映射见：

- [TASK_RESOURCE_MAP.md](/E:/STM32_CODE_WORK/gradute_work/FInal_graduate_work/Code/01_Task/TASK_RESOURCE_MAP.md)

---

## 5. 逐任务详解

## 5.1 `InitTask`（系统装配任务）

核心工作：

1. 静态绑定表注入：`mod_led/mod_relay/mod_motor/mod_sensor`。
2. 模块初始化：LED、继电器、按键、电机、传感器。
3. 协议上下文绑定：
   - VOFA -> `huart3` + `PcMutexHandle`
   - K230 -> `huart4`
   - Stepper 双轴 -> `huart5`（X）+ `huart2`（Y）
4. 释放 `Sem_InitHandle`，然后 `osThreadTerminate` 自删除。

解耦点：

1. 业务任务不持有硬件绑定细节。
2. Task 运行期只消费“已绑定上下文”，不关心谁创建。

扩展建议：

1. 新增模块时优先在本任务做 bind/init 注入。
2. 若模块初始化可能失败，保留返回码并在状态页输出绑定状态。

## 5.2 `KeyTask`（输入事件源）

核心工作：

1. `mod_key_scan()` 周期扫描事件。
2. 所有有效按键先统一释放 `Sem_RedLEDHandle` 做反馈。
3. 再将特定事件映射到 DCC 控制信号量。

映射规则：

1. KEY2 单击 -> `Sem_DccHandle`
2. KEY3 单击 -> `Sem_TaskChangeHandle`
3. KEY3 双击 -> `Sem_ReadyToggleHandle`

解耦点：

1. 按键任务只发事件，不直接改 DCC 状态机。
2. 任务间通过信号量通信，避免直接函数耦合。

## 5.3 `DccTask`（底盘主状态机）

核心状态：

1. 模式：`IDLE/STRAIGHT/TRACK`
2. 运行态：`OFF/PREPARE/ON/STOP`

控制路径：

1. `STRAIGHT`：位置环 + 双速度环，含 1 秒传感器保护窗。
2. `TRACK`：按传感器权重分配双轮目标速度。
3. `STOP`：保护停机并回 `IDLE`。

对外接口：

1. `task_dcc_get_mode()`
2. `task_dcc_get_run_state()`
3. `task_dcc_get_ready()`

解耦点：

1. 对外只暴露只读查询，不暴露写接口。
2. 控制执行只通过 `mod_motor/mod_sensor` 抽象完成。

## 5.4 `GpioTask`（反馈执行任务）

职责：

1. 消费 `Sem_RedLEDHandle` 做短时黄灯/蜂鸣反馈。
2. 读取 `task_dcc_get_run_state()` 决定：
   - ON：绿灯闪烁、激光开启。
   - STOP：红灯闪烁、蜂鸣节奏告警。
   - OFF/PREPARE：关闭状态输出。

解耦点：

1. 只读 DCC 状态，不改 DCC 状态。
2. 仅调用 `mod_led/mod_relay`，不触及底层 GPIO 细节。

## 5.5 `StepperTask`（视觉闭环任务）

职责：

1. 轮询 `mod_k230_get_latest_frame()` 获取误差帧。
2. 按 DCC 模式执行位置控制与丢帧容错。
3. 通过 `mod_stepper_position/stop` 下发协议命令。
4. 通过 `mod_vofa_send_float_ctx()` 上报调试数据。

关键保护：

1. 连续无新帧达到阈值后保护停机。
2. 软限位保护 `X/Y` 轴。
3. 小误差区 Kp-only，抑制积分抖动。

解耦点：

1. 视觉协议、步进协议、上报协议都在 Module 层。
2. Task 只做控制策略，不做协议拼帧。

## 5.6 `OledTask`（显示任务）

职责：

1. 周期更新电池电压缓存。
2. 周期读取 DCC 模式与运行态并刷新页面。
3. 页面内容写入通过 `OLED_*` 接口完成。

解耦点：

1. 显示任务不参与控制决策。
2. 电压数据来源于 `mod_battery`，显示通道来源于 `mod_oled`。

## 5.7 `TestTask`（调试预留）

职责：

1. 当前默认空转延时，保留调试模板。
2. 可按需临时启用 VOFA 或传感器联调代码。

维护建议：

1. 调试完成后恢复空转，避免污染正式控制链路。
2. 不把关键业务逻辑长期放在本任务。

## 5.8 `defaultTask`

职责：

1. 调用 `task_wait_init_done()` 后保持最小调度负载。
2. 作为系统保底线程存在。

---

## 6. Task 层解耦设计模式

## 6.1 模式一：初始化闸门

1. `InitTask` 负责“一次性装配”。
2. 其它任务统一调用 `task_wait_init_done()`。
3. 闸门通过后保持常开，后续任务快速通过。

## 6.2 模式二：事件信号量解耦

1. 输入任务（Key）只投递信号量。
2. 业务任务（Dcc/Gpio）各自消费并处理。
3. 避免“输入层直接写业务状态”带来的强耦合。

## 6.3 模式三：状态只读接口

1. 共享状态通过 `task_dcc_get_*` 提供。
2. 外部任务禁止写入 DCC 内部状态变量。
3. 降低跨任务竞态风险。

---

## 7. 扩展开发指南（Task 层）

新增任务建议步骤：

1. 在 `freertos.c` 新建线程属性（栈、优先级、入口）。
2. 在 `Code/01_Task/Inc` 增加头文件并声明入口。
3. 在 `Code/01_Task/Src` 实现任务逻辑并先调用 `task_wait_init_done()`。
4. 若需跨任务通信，优先新增信号量而非直接互调。
5. 更新 `TASK_RESOURCE_MAP.md` 与本说明文档。

调参建议：

1. 周期参数优先在头文件宏集中管理。
2. PID 参数改动需保留“默认值 + 场景说明”。
3. 调参后做最小回归：启动、按键、模式切换、保护停机。

---

## 8. 维护与排障清单

日常维护检查：

1. 所有任务是否都通过初始化闸门。
2. 信号量是否存在“生产者释放但无消费者”。
3. 高优先级任务是否存在过长阻塞调用。
4. 状态机分支是否覆盖异常输入。

常见问题定位：

1. 任务不运行：先查 `Sem_InitHandle` 是否释放。
2. 按键无效：查 `KeyTask` 扫描周期与 `mod_key` 配置是否一致。
3. 底盘不转：查 DCC 运行态是否进入 `ON`，再查 `mod_motor` 绑定。
4. 步进无动作：查 K230 帧更新计数、轴绑定状态、UART 归属冲突。
5. 无上报：查 VOFA 绑定、互斥锁、串口状态是否空闲。

---

## 9. 版本管理建议

建议在后续维护中遵循：

1. Task 层接口变更必须同步更新本文件和 `TASK_RESOURCE_MAP.md`。
2. 每次新增任务或同步资源时，更新“任务总览表”和“同步资源表”。
3. 与 Module/Driver 的边界调整必须注明“谁上移/谁下移”与原因。
