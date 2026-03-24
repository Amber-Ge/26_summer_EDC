# Core 启动层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Core Bootstrap Layer Guide |
| 覆盖范围 | `Core/Inc` + `Core/Src` + `startup_stm32f407xx.s` |
| 作者 | ST 生成代码（基础框架） / 姜凯中（用户区业务装配） / Codex（文档整理） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |

---

## 1. 层定位

Core 启动层是“系统引导与外设初始化层”，负责把 MCU 从复位态带到 RTOS 可运行态。

Core 层负责：

1. 时钟树配置与 HAL 初始化。
2. 外设初始化函数调用（GPIO/UART/TIM/ADC/I2C/DMA）。
3. RTOS 对象创建与任务启动入口。

Core 层不负责：

1. 业务控制状态机（归 Task 层）。
2. 设备语义抽象（归 Module 层）。
3. 驱动复用封装（归 Driver 层）。

---

## 2. 启动主链路

`main.c` 中关键顺序：

1. `HAL_Init()`
2. `SystemClock_Config()`
3. 外设初始化：
   - `MX_GPIO_Init()`
   - `MX_DMA_Init()`
   - `MX_USART3_UART_Init()`
   - `MX_ADC1_Init()`
   - `MX_USART2_UART_Init()`
   - `MX_TIM4_Init()`
   - `MX_UART5_Init()`
   - `MX_TIM2_Init()`
   - `MX_TIM3_Init()`
   - `MX_I2C2_Init()`
   - `MX_UART4_Init()`
4. `osKernelInitialize()`
5. `MX_FREERTOS_Init()`
6. `osKernelStart()`

含义：

1. Core 只负责“把平台准备好”。
2. 业务模块资源注入在 `InitTask` 执行，不塞入 `main.c`。

---

## 3. `freertos.c` 的关键职责

`Core/Src/freertos.c` 负责：

1. 创建任务对象与属性（栈、优先级）。
2. 创建同步对象（互斥锁与信号量）。
3. 启动线程入口函数。

当前关键对象：

1. 任务：`default/Gpio/Key/Oled/Test/Stepper/Dcc/Init`
2. 互斥锁：`PcMutexHandle`
3. 信号量：`Sem_RedLED/Sem_Dcc/Sem_TaskChange/Sem_Init/Sem_ReadyToggle`

桥接点：

1. `defaultTask` 调用 `task_wait_init_done()`，与 Task 层初始化闸门衔接。

---

## 4. Core 与上层的解耦策略

## 4.1 生成代码与用户代码分区

1. Core 文件由 CubeMX 生成，用户业务尽量放到 `USER CODE BEGIN/END` 区域。
2. 业务算法与协议逻辑不直接写在 Core 外设文件。

## 4.2 初始化职责分离

1. Core 只做外设句柄初始化。
2. 具体“哪个模块绑定哪个句柄”由 `InitTask` 决定。

## 4.3 资源对象上移到 RTOS 层

1. Core 负责创建 `PcMutex` 和业务信号量。
2. Task/Module 通过 `extern` 使用，避免重复创建与生命周期混乱。

---

## 5. 新增外设时的推荐流程

1. 先通过 CubeMX 生成/更新 `Core/Inc` 与 `Core/Src` 初始化函数。
2. 在 `main.c` 增加 `MX_xxx_Init()` 调用顺序。
3. 在 Task `InitTask` 中完成模块层 bind 注入。
4. 在 Module/Driver 层新增对应抽象，不在 Core 里写业务调用。

---

## 6. 维护风险与建议

风险点：

1. 直接修改 CubeMX 自动区，后续重新生成可能丢失。
2. Core 层写入业务逻辑，导致后续难以回归和分层维护。
3. 任务对象和同步对象命名变化后，上层 `extern` 断链。

建议：

1. Core 变更优先保持“初始化职责”，不扩散业务逻辑。
2. RTOS 对象名变更时同时更新 Task 层声明和文档。
3. 每次 CubeMX 重新生成后先做一次最小构建验证。

---

## 7. 快速排障

1. 系统启动后无任务运行：
   - 检查 `osKernelStart()` 是否执行。
   - 检查 `MX_FREERTOS_Init()` 是否创建线程成功。
2. 某外设不可用：
   - 检查 `MX_xxx_Init()` 是否在 `main.c` 调用。
   - 检查对应句柄是否被正确注入模块层。
3. 串口输出冲突：
   - 检查上层是否使用 `PcMutexHandle` 序列化发送。
