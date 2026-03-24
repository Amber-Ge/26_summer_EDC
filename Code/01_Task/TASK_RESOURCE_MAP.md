# Task 层资源与职责说明

## 1. 文档范围

- 目录范围：`Code/01_Task`
- 线程创建来源：`Core/Src/freertos.c`
- 目的：明确每个任务的栈大小、优先级、同步资源（信号量/互斥锁）以及职责边界，便于后续扩展和排障。

## 2. 任务线程配置总表

| 任务名 | 入口函数 | 栈大小（字节） | 优先级 | 生命周期 | 主要职责 |
| --- | --- | ---: | --- | --- | --- |
| `defaultTask` | `StartDefaultTask` | `128 * 4 = 512` | `osPriorityLow` | 常驻 | 等待 `InitTask` 完成后维持最小调度节拍。 |
| `GpioTask` | `StartGpioTask` | `128 * 4 = 512` | `osPriorityLow` | 常驻 | 灯光/蜂鸣器/激光继电器状态机。 |
| `KeyTask` | `StartKeyTask` | `128 * 4 = 512` | `osPriorityLow` | 常驻 | 按键扫描并分发控制事件。 |
| `OledTask` | `StartOledTask` | `128 * 4 = 512` | `osPriorityLow` | 常驻 | OLED 刷新与电池电压显示。 |
| `TestTask` | `StartTestTask` | `128 * 4 = 512` | `osPriorityHigh` | 常驻 | 测试预留任务（当前主要空转延时）。 |
| `StepperTask` | `StartStepperTask` | `1024 * 4 = 4096` | `osPriorityRealtime` | 常驻 | 视觉误差闭环控制步进轴，处理丢帧保护。 |
| `DccTask` | `StartDccTask` | `512 * 4 = 2048` | `osPriorityRealtime` | 常驻 | 底盘 DCC 模式/运行态状态机和 PID 控制。 |
| `InitTask` | `StartInitTask` | `256 * 4 = 1024` | `osPriorityRealtime7` | 一次性 | 系统装配初始化，完成后释放闸门并自删除。 |

## 3. 同步资源定义总表

| 资源名 | 类型 | 初始值 | 生产者 | 消费者 | 作用 |
| --- | --- | ---: | --- | --- | --- |
| `Sem_InitHandle` | 二值信号量 | `0` | `InitTask` | 所有业务任务（通过 `task_wait_init_done`） | 系统初始化闸门。 |
| `Sem_RedLEDHandle` | 二值信号量 | `0` | `KeyTask` | `GpioTask` | 按键反馈触发黄灯短闪+短鸣。 |
| `Sem_DccHandle` | 二值信号量 | `0` | `KeyTask` | `DccTask` | KEY2 单击触发 DCC 全重置。 |
| `Sem_TaskChangeHandle` | 二值信号量 | `0` | `KeyTask` | `DccTask` | KEY3 单击切换 DCC 模式。 |
| `Sem_ReadyToggleHandle` | 二值信号量 | `0` | `KeyTask` | `DccTask` | KEY3 双击切换 DCC 运行态。 |
| `PcMutexHandle` | 互斥锁 | N/A | `freertos.c` 创建 | `mod_vofa`、`mod_stepper` 发送路径 | 保护多任务串口发送互斥。 |

## 4. 各任务资源与边界

### 4.1 `InitTask`

- 直接使用资源：
- `Sem_InitHandle`：初始化完成后释放一次，打开全系统闸门。
- `PcMutexHandle`：注入给 VOFA/Stepper 绑定结构，用于后续发送互斥。
- 关键边界：
- 只负责“装配与初始化”，不承担运行期业务控制。
- 结束时调用 `osThreadTerminate` 自删除，避免长期占用调度资源。

### 4.2 `defaultTask`

- 直接使用资源：
- 无直接信号量/互斥锁操作。
- 间接依赖：
- 调用 `task_wait_init_done`，通过 `Sem_InitHandle` 门控后进入空转延时。
- 关键边界：
- 不承载业务逻辑，仅作为系统保底线程。

### 4.3 `KeyTask`

- 直接使用资源：
- 释放 `Sem_RedLEDHandle`（任意有效按键事件）。
- 释放 `Sem_DccHandle`（KEY2 单击）。
- 释放 `Sem_TaskChangeHandle`（KEY3 单击）。
- 释放 `Sem_ReadyToggleHandle`（KEY3 双击）。
- 关键边界：
- 只做事件采集和分发，不直接操作底盘状态机。

### 4.4 `DccTask`

- 直接使用资源：
- 轮询消费 `Sem_DccHandle`、`Sem_TaskChangeHandle`、`Sem_ReadyToggleHandle`。
- 关键边界：
- 对外暴露 `task_dcc_get_mode/run_state/ready` 只读状态接口。
- 负责模式切换、运行态切换、PID 驱动与保护停机，不处理 UI 输出。

### 4.5 `GpioTask`

- 直接使用资源：
- 消费 `Sem_RedLEDHandle` 触发黄灯脉冲和短鸣。
- 间接依赖：
- 读取 `task_dcc_get_run_state` 决定绿灯/红灯/蜂鸣器/激光策略。
- 关键边界：
- 只负责人机反馈外设输出，不修改 DCC 内部状态。

### 4.6 `OledTask`

- 直接使用资源：
- 无直接信号量/互斥锁。
- 间接依赖：
- 读取 `task_dcc_get_mode`、`task_dcc_get_run_state`。
- 关键边界：
- 只做显示采样与刷新，不参与控制闭环。

### 4.7 `StepperTask`

- 直接使用资源：
- 无直接 `osMutexAcquire` 调用，但通过 `task_stepper_prepare_channels` 绑定 `PcMutexHandle` 到 `mod_stepper`。
- 间接依赖：
- 读取 DCC 模式/运行态决定是否开启闭环。
- 读取 K230 最新帧执行双轴控制，丢帧后进入容忍或保护停机。
- 关键边界：
- 只负责步进轴闭环和通道状态维护，不负责创建全局同步资源。

### 4.8 `TestTask`

- 直接使用资源：
- 无信号量/互斥锁（当前测试逻辑注释保留，任务主要延时）。
- 关键边界：
- 调试预留，不影响主业务链路。

## 5. 资源流向简图

| 事件源 | 资源 | 目标任务 | 效果 |
| --- | --- | --- | --- |
| 按键事件（`KeyTask`） | `Sem_RedLEDHandle` | `GpioTask` | 黄灯短闪 + 蜂鸣短鸣反馈。 |
| KEY2 单击（`KeyTask`） | `Sem_DccHandle` | `DccTask` | 模式/运行态全重置到 OFF。 |
| KEY3 单击（`KeyTask`） | `Sem_TaskChangeHandle` | `DccTask` | 切换 DCC 模式。 |
| KEY3 双击（`KeyTask`） | `Sem_ReadyToggleHandle` | `DccTask` | OFF/PREPARE/ON 运行态切换。 |
| 初始化完成（`InitTask`） | `Sem_InitHandle` | 全业务任务 | 放行任务主循环。 |

