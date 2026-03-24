# Task 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | TASK_LAYER_GUIDE |
| 所在层 | `Code/01_Task` |
| 作者 | 姜凯中 |
| 版本 | v1.00 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |
| 配置来源 | `Core/Src/freertos.c` + `Code/01_Task` |

---

## 1. 层定位与边界

Task 层是系统业务调度层，核心职责是把“输入事件、状态机、时序、模块调用”组织成稳定可维护的任务链路。

Task 层负责：

1. 任务生命周期组织（常驻任务与一次性初始化任务）。
2. 跨任务同步资源消费与分发（信号量、互斥锁）。
3. 业务状态机维护（DCC 模式与运行状态）。
4. 调用 Module 层 API 形成完整业务流程。

Task 层不负责：

1. HAL 底层寄存器访问。
2. Driver/Module 内部实现细节。
3. 硬件映射定义（由 InitTask 绑定注入）。

---

## 2. 任务与资源总览

### 2.1 任务配置表（与 `freertos.c` 一致）

| 任务名 | 入口函数 | 栈配置 | 栈字节 | 优先级 | 生命周期 | 主要职责 |
| --- | --- | --- | ---: | --- | --- | --- |
| `InitTask` | `StartInitTask` | `256*4` | `1024` | `osPriorityRealtime7` | 一次性 | 模块绑定、初始化、释放启动闸门 |
| `StepperTask` | `StartStepperTask` | `1024*4` | `4096` | `osPriorityRealtime` | 常驻 | 双轴闭环控制、视觉帧消费、状态上报 |
| `DccTask` | `StartDccTask` | `512*4` | `2048` | `osPriorityRealtime` | 常驻 | DCC 状态机与底盘控制 |
| `TestTask` | `StartTestTask` | `128*4` | `512` | `osPriorityHigh` | 常驻 | 测试预留任务 |
| `defaultTask` | `StartDefaultTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 保底空转任务 |
| `GpioTask` | `StartGpioTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 灯光/蜂鸣/激光输出仲裁 |
| `KeyTask` | `StartKeyTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 按键扫描与事件分发 |
| `OledTask` | `StartOledTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 页面刷新与状态显示 |

### 2.2 同步资源表

| 资源名 | 类型 | 初始值 | 生产者 | 消费者 | 作用 |
| --- | --- | ---: | --- | --- | --- |
| `Sem_InitHandle` | Semaphore | `0` | `InitTask` | 所有业务任务（`task_wait_init_done`） | 系统初始化闸门 |
| `Sem_RedLEDHandle` | Semaphore | `0` | `KeyTask` | `GpioTask` | 按键反馈灯/蜂鸣触发 |
| `Sem_DccHandle` | Semaphore | `0` | `KeyTask` | `DccTask` | KEY2 触发 DCC 全重置 |
| `Sem_TaskChangeHandle` | Semaphore | `0` | `KeyTask` | `DccTask` | KEY3 单击切换 DCC 模式 |
| `Sem_ReadyToggleHandle` | Semaphore | `0` | `KeyTask` | `DccTask` | KEY3 双击切换 DCC 运行态 |
| `PcMutexHandle` | Mutex | N/A | InitTask 注入到模块 | 串口发送相关模块 | 串口发送互斥（避免并发冲突） |

---

## 3. 系统启动时序

1. RTOS 创建互斥锁、信号量和全部线程。
2. `InitTask` 最先执行，完成模块绑定与初始化。
3. `InitTask` 完成后释放 `Sem_InitHandle`。
4. 各业务任务在主循环前调用 `task_wait_init_done()` 通过闸门。
5. `InitTask` 执行 `osThreadTerminate` 自删除，避免重复初始化。

该机制保证“初始化先于业务运行”，避免任务抢跑访问未绑定资源。

---

## 4. 任务调用链

1. 输入链路：`KeyTask -> mod_key_scan -> 信号量分发`。
2. 底盘链路：`DccTask -> mod_motor + mod_sensor + PID`。
3. 视觉链路：`StepperTask -> mod_k230 -> mod_stepper`。
4. 输出链路：`GpioTask -> mod_led + mod_relay`。
5. 显示链路：`OledTask -> mod_battery + mod_oled`。

---

## 5. 文件级说明

### 5.1 `task_init.c`

职责：

1. 维护全局模块绑定表与默认上下文注入。
2. 顺序执行模块初始化并写入上电安全态。
3. 绑定通信相关通道并释放 `Sem_InitHandle`。

维护要点：

1. 新模块必须遵循“先 bind，后 init”。
2. 初始化失败处理不能破坏启动闸门策略。
3. 该任务应保持一次性，不承载运行期业务逻辑。

### 5.2 `task_key.c`

职责：

1. 周期扫描按键模块输出事件。
2. 将事件映射为业务信号量。
3. 对任意有效事件释放 `Sem_RedLEDHandle` 触发反馈。

事件映射：

1. `MOD_KEY_EVENT_2_CLICK -> Sem_DccHandle`。
2. `MOD_KEY_EVENT_3_CLICK -> Sem_TaskChangeHandle`。
3. `MOD_KEY_EVENT_3_DOUBLE_CLICK -> Sem_ReadyToggleHandle`。

### 5.3 `task_dcc.c`

职责：

1. DCC 状态机唯一写入者。
2. 消费按键信号量并驱动状态转移。
3. 在 ON 态按模式执行控制分支。

核心状态：

1. 模式：`IDLE/STRAIGHT/TRACK`。
2. 运行态：`OFF/PREPARE/ON/STOP`。

维护要点：

1. 新增状态必须保持“单写入者”规则。
2. 保护逻辑建议封装为静态函数，避免主循环膨胀。

### 5.4 `task_stepper.c`

职责：

1. 双轴步进控制任务。
2. 消费视觉帧误差并分发到 X/Y 轴。
3. 执行死区、限位、丢帧容错与停机保护。

维护要点：

1. 新增轴需同步扩展状态快照与上报字段。
2. 限位参数调整必须与机械行程实测一致。

### 5.5 `task_gpio.c`

职责：

1. 统一仲裁黄灯、红绿灯、蜂鸣器、激光继电器输出。
2. 根据 DCC 运行态执行灯效与蜂鸣节奏。
3. 消费 `Sem_RedLEDHandle` 提供按键反馈。

维护要点：

1. 所有灯光/蜂鸣策略应集中在本任务，避免跨任务直接写 GPIO。

### 5.6 `task_oled.c`

职责：

1. 周期采样电压并缓存。
2. 周期刷新 OLED 页面。
3. 显示 DCC 模式与运行状态。

维护要点：

1. 保持采样周期与刷新周期解耦，减少无效 I2C 压力。

### 5.7 `task_test.c`

职责：

1. 预留联调任务。
2. 默认空转，不进入正式业务链路。

维护要点：

1. 临时测试逻辑必须可快速回退并恢复默认空转。

---

## 6. Task 层开关配置

### 6.1 任务启动开关（除 `InitTask` 外）

| 任务 | 宏名 | 默认值 | 说明 |
| --- | --- | ---: | --- |
| DccTask | `TASK_DCC_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |
| StepperTask | `TASK_STEPPER_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |
| GpioTask | `TASK_GPIO_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |
| KeyTask | `TASK_KEY_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |
| OledTask | `TASK_OLED_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |
| TestTask | `TASK_TEST_STARTUP_ENABLE` | `1` | `0` 时任务启动后挂起 |

### 6.2 VOFA 上报开关

| 任务 | 宏名 | 默认值 | 说明 |
| --- | --- | ---: | --- |
| DccTask | `TASK_DCC_VOFA_ENABLE` | `0` | 控制 DCC 状态数据上报 |
| StepperTask | `TASK_STEPPER_VOFA_ENABLE` | `1` | 控制步进误差数据上报 |

---

## 7. 代码风格与注释规范（Task 层）

1. 文件头统一包含：`@file/@author/@version/@date/@brief/@details`。
2. 对外接口函数必须写明参数与返回语义。
3. 关键静态函数必须写明输入、判定、输出与副作用。
4. 关键变量注明单位和用途（ms/tick/pulse/rpm）。
5. 状态机和保护逻辑必须有步骤注释。

---

## 8. 扩展指南

1. 新增任务：先在 `freertos.c` 创建线程与资源，再补 `task_xxx.h/.c`。
2. 新增任务必须调用 `task_wait_init_done()` 进行启动闸门同步。
3. 新增同步资源后必须回填本文件资源总表。
4. 新增 DCC/Stepper 模式时，同步更新 OLED 页面和本文件说明。

---

## 9. 维护与回归检查清单

1. 是否仍保持 Init 闸门先于业务主循环。
2. 是否保持 DCC 状态机单写入者。
3. 是否出现跨任务直接操作硬件输出的新增路径。
4. 是否新增未文档化的信号量/互斥锁。
5. 是否修改控制周期后未评估实时性与栈占用。

---

## 10. 版本记录

### v1.00

1. 建立 Task 层单文档详解手册。
2. 补齐任务栈、优先级、同步资源和调用链说明。
3. 补齐任务开关与 VOFA 开关统一说明。
