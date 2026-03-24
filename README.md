# FInal_graduate_work 项目总引言（README）

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | README |
| 文档定位 | 项目总入口 + 新手上手手册 + 分层规范 + 维护治理规范 |
| 作者 | 姜凯中 |
| 版本 | V1.00 |
| 最后更新 | 2026-03-24 |
| 文件编码 | UTF-8 |
| 适用工程 | `FInal_graduate_work` |

---

## 0. 先读这一段

本 README 是仓库最外层总文档，作用不是“简单介绍”，而是：

1. 让完全新手可以按步骤跑通工程。
2. 让驱动层和模块层开发者知道统一架构怎么落地。
3. 让后续维护者清楚“哪些地方改了，文档必须同步改哪里”。
4. 让多作者长期协作时，版本和接口演进不会失控。

> 强制规则：涉及任务配置、引脚映射、外设绑定、模块调用链的改动，必须同步更新本 README 对应章节。

---

## 1. 工程目标与分层理念

### 1.1 工程目标

本项目是基于 STM32 的分层式 C 工程，目标是：

1. 低耦合：硬件驱动与业务逻辑分离。
2. 高可维护：新增设备或协议时，不需要推倒重来。
3. 可扩展：后续新增作者也能按同一规范持续演进。

### 1.2 统一依赖方向

严格遵循单向依赖：

1. `Task -> Module -> Driver -> HAL`
2. 禁止反向依赖与跨层绕过调用。

---

## 2. 新手 30 分钟上手路径

### 2.1 环境准备

1. `arm-none-eabi-gcc`
2. `CMake`
3. `Ninja`
4. ST-Link 下载工具
5. 串口调试工具

### 2.2 编译命令

```powershell
cmake --preset Debug
cmake --build --preset Debug
```

### 2.3 产物位置

1. `build/Debug/FInal_graduate_work.elf`

### 2.4 首次运行检查

1. 程序可下载并启动。
2. 任务能正常进入循环。
3. 串口通信链路无明显冲突。
4. 关键控制任务无异常阻塞。

---

## 3. 仓库结构（总览）

1. `Code/01_Task`：任务调度层（状态机、时序、协同）
2. `Code/02_Module`：模块语义层（协议/设备语义封装）
3. `Code/03_Common`：通用算法与工具层
4. `Code/04_Driver`：底层驱动抽象层
5. `Core`：CubeMX 生成层（系统初始化、外设初始化、中断）
6. `Drivers`、`Middlewares`：厂商库和中间件

---

## 4. 各层职责边界（必须遵守）

### 4.1 Task 层

允许：

1. 任务调度与状态机编排
2. 调用模块接口组成业务流程
3. 跨任务同步（信号量/互斥）

禁止：

1. 直接访问 HAL 外设
2. 直接调用 Driver 绕过 Module

### 4.2 Module 层

允许：

1. 把 Driver 能力组装成业务语义接口
2. 管理 `ctx/bind/unbind/is_bound`
3. 管理运行缓存、状态、统计计数

禁止：

1. 承担任务调度职责
2. 混入 HAL 细节实现

### 4.3 Driver 层

允许：

1. 外设原子能力封装
2. 参数/状态校验
3. 错误码与状态码返回

禁止：

1. 业务语义命名与业务流程编排
2. 直接做任务层逻辑

---

## 5. 当前任务配置清单（强制同步区）

> 如果修改 `freertos.c` 中任务入口、栈、优先级、信号量，请同步更新本章与 `Code/01_Task/TASK_LAYER_GUIDE.md`。

### 5.1 线程配置总表（来源：`Core/Src/freertos.c`）

| 任务名 | 入口函数 | 栈配置 | 栈字节 | 优先级 | 生命周期 | 当前职责 |
| --- | --- | --- | ---: | --- | --- | --- |
| `defaultTask` | `StartDefaultTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 保底空转，等待初始化闸门 |
| `GpioTask` | `StartGpioTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | LED/蜂鸣器/激光输出仲裁 |
| `KeyTask` | `StartKeyTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | 按键扫描与事件分发 |
| `OledTask` | `StartOledTask` | `128*4` | `512` | `osPriorityLow` | 常驻 | OLED 页面刷新与电压显示 |
| `TestTask` | `StartTestTask` | `128*4` | `512` | `osPriorityHigh` | 常驻 | 联调预留 |
| `StepperTask` | `StartStepperTask` | `1024*4` | `4096` | `osPriorityRealtime` | 常驻 | 步进双轴控制 |
| `DccTask` | `StartDccTask` | `512*4` | `2048` | `osPriorityRealtime` | 常驻 | 底盘控制与状态机 |
| `InitTask` | `StartInitTask` | `256*4` | `1024` | `osPriorityRealtime7` | 一次性 | 模块绑定初始化，释放启动闸门后自删除 |

### 5.2 同步资源总表（来源：`Core/Src/freertos.c`）

| 资源名 | 类型 | 初始值 | 当前用途 |
| --- | --- | ---: | --- |
| `PcMutexHandle` | Mutex | N/A | 串口发送互斥 |
| `Sem_RedLEDHandle` | Semaphore | 0 | 按键反馈触发 GPIO 提示 |
| `Sem_DccHandle` | Semaphore | 0 | DCC 相关触发 |
| `Sem_TaskChangeHandle` | Semaphore | 0 | 模式切换触发 |
| `Sem_InitHandle` | Semaphore | 0 | 初始化闸门 |
| `Sem_ReadyToggleHandle` | Semaphore | 0 | 运行态切换触发 |

---

## 6. 当前引脚与外设映射（强制同步区）

> 如果修改 `main.h/usart.c/tim.c/adc.c/i2c.c/task_init.c/task_stepper.c` 相关映射，必须同步更新本章。

### 6.1 GPIO 功能引脚表（来源：`Core/Inc/main.h`）

| 功能 | 引脚 |
| --- | --- |
| 激光继电器 | `PE2` (`Laser_Pin`) |
| 蜂鸣器继电器 | `PE3` (`Buzzer_Pin`) |
| 红灯 | `PF10` (`LED_RED_Pin`) |
| 绿灯 | `PF11` (`LED_GREEN_Pin`) |
| 黄灯 | `PF12` (`LED_YELLOW_Pin`) |
| 左电机方向 AIN1 | `PE7` |
| 左电机方向 AIN2 | `PE8` |
| 右电机方向 BIN1 | `PE9` |
| 右电机方向 BIN2 | `PE10` |
| 按键 KEY1 | `PG2` |
| 按键 KEY2 | `PG3` |
| 按键 KEY3 | `PG4` |
| 传感器通道 1~12 | `PG0, PG1, PG5~PG14` |

### 6.2 通信总线引脚表（来源：`Core/Src/usart.c`、`Core/Src/i2c.c`）

| 外设 | TX/SDA | RX/SCL | 参数 | 当前绑定用途 |
| --- | --- | --- | --- | --- |
| `UART4` | `PC10` | `PA1` | `115200` | `mod_k230` |
| `UART5` | `PC12` | `PD2` | `115200` | Stepper X 轴 |
| `USART2` | `PD5` | `PD6` | `115200` | Stepper Y 轴 |
| `USART3` | `PB10` | `PB11` | `115200` | `mod_vofa` |
| `I2C2` | `PF0` | `PF1` | `400kHz` | OLED |

### 6.3 电机与采样相关引脚（来源：`Core/Src/tim.c`、`Core/Src/adc.c`）

| 功能 | 外设/通道 | 引脚 | 当前用途 |
| --- | --- | --- | --- |
| 左电机 PWM | `TIM4_CH1` | `PD12` | 左轮占空比输出 |
| 右电机 PWM | `TIM4_CH2` | `PD13` | 右轮占空比输出 |
| 左编码器 | `TIM2_CH1/CH2` | `PA15/PB3` | 左轮编码器反馈 |
| 右编码器 | `TIM3_CH1/CH2` | `PB4/PB5` | 右轮编码器反馈 |
| 电池电压 ADC | `ADC1_IN2` | `PA2` | 电池电压采样 |

### 6.4 模块绑定关系（来源：`Code/01_Task/Src/task_init.c`、`task_stepper.c`）

| 模块/对象 | 绑定资源 | 关键参数 |
| --- | --- | --- |
| `mod_vofa` | `huart3` | `tx_mutex = PcMutexHandle` |
| `mod_k230` | `huart4` | `checksum = XOR` |
| `Stepper X` | `huart5` | `driver_addr = 1` |
| `Stepper Y` | `huart2` | `driver_addr = 1` |
| `mod_battery` | `hadc1` | 默认采样与换算参数 |
| OLED | `hi2c2` | `OLED_BindI2C(default)` |
| 左电机 | `TIM4_CH1 + TIM2 + AIN2/AIN1` | 驱动 + 编码器 |
| 右电机 | `TIM4_CH2 + TIM3 + BIN1/BIN2` | 驱动 + 编码器 |

### 6.5 12 路循迹权重（来源：`Code/01_Task/Src/task_init.c`）

| 通道 | 引脚 | 权重 |
| --- | --- | ---: |
| 1 | `PG0` | `0.60` |
| 2 | `PG1` | `0.40` |
| 3 | `PG5` | `0.30` |
| 4 | `PG6` | `0.20` |
| 5 | `PG7` | `0.10` |
| 6 | `PG8` | `0.05` |
| 7 | `PG9` | `-0.05` |
| 8 | `PG10` | `-0.10` |
| 9 | `PG11` | `-0.20` |
| 10 | `PG12` | `-0.30` |
| 11 | `PG13` | `-0.40` |
| 12 | `PG14` | `-0.60` |

---

## 7. 扩展开发指南（Driver 与 Module）

### 7.1 新增 Driver 的建议流程

1. 在 `Code/04_Driver/Inc` 新建 `drv_xxx.h`。
2. 在 `Code/04_Driver/Src` 新建 `drv_xxx.c`。
3. 定义 `ctx/cfg/status`，实现 `ctx_init/start/stop/run`。
4. 在 `CMakeLists.txt` 添加新源文件。
5. 先做参数与状态回归，再让 Module 接入。

### 7.2 新增 Module 的建议流程

1. 在 `Code/02_Module/Inc` 新建 `mod_xxx.h`。
2. 在 `Code/02_Module/Src` 新建 `mod_xxx.c`。
3. 定义 `bind_t` 注入映射，定义 `ctx_t` 管理状态。
4. 实现 `ctx_init/bind/unbind/is_bound/运行API`。
5. 在 `task_init.c` 接入初始化绑定。
6. 任务层仅调用 Module API，不跨层调用 Driver。

### 7.3 串口类模块统一要求

1. 必须走 `drv_uart`，不要直接 HAL UART。
2. 必须接入 `mod_uart_guard` 做归属仲裁。
3. 必须统一 `tx_mutex` 做发送互斥。
4. 必须在解绑时完整释放回调和占用关系。

---

## 8. 后续维护者改动提醒（务必执行）

### 8.1 改任务配置时，至少同步这些文件

1. `Core/Src/freertos.c`
2. `Code/01_Task/TASK_LAYER_GUIDE.md`
3. `README.md` 第 5 章

### 8.2 改引脚或外设映射时，至少同步这些文件

1. `Core/Inc/main.h`
2. `Core/Src/usart.c`
3. `Core/Src/tim.c`
4. `Core/Src/adc.c`
5. `Core/Src/i2c.c`
6. `Code/01_Task/Src/task_init.c`
7. `Code/01_Task/Src/task_stepper.c`
8. `README.md` 第 6 章

### 8.3 改分层接口时，至少同步这些文件

1. `Code/04_Driver/DRIVER_LAYER_GUIDE.md`
2. `Code/02_Module/MODULE_LAYER_GUIDE.md`
3. `Code/03_Common/COMMON_LAYER_GUIDE.md`
4. `Code/01_Task/TASK_LAYER_GUIDE.md`
5. `README.md`（跨层规则变更时）

---

## 9. 多作者协作与版本治理

### 9.1 版本规则

1. 兼容性增强：`Vx.yy -> Vx.(yy+1)`
2. 破坏性变更：`Vx.yy -> V(x+1).00`

### 9.2 作者登记表（可持续扩展）

| 作者 | 角色 | 负责范围 | 首次参与版本 | 状态 |
| --- | --- | --- | --- | --- |
| 姜凯中 | 架构维护者 | 全局 | V1.00 | Active |

> 后续新增作者请追加行，不要覆盖历史记录。

### 9.3 变更记录模板

```markdown
### [Vx.yy] - YYYY-MM-DD - 作者名
1. 变更目标：
2. 影响层级：
3. 兼容性影响：
4. 验证方式与结果：
5. 遗留事项：
```

### 9.4 PR 自检清单

1. 是否违反分层边界。
2. 是否更新了对应层文档。
3. 是否补充了回归验证结果。
4. 是否把任务/引脚改动同步到 README。

---

## 10. 关联文档索引

1. `Code/04_Driver/DRIVER_LAYER_GUIDE.md`
2. `Code/02_Module/MODULE_LAYER_GUIDE.md`
3. `Code/03_Common/COMMON_LAYER_GUIDE.md`
4. `Code/01_Task/TASK_LAYER_GUIDE.md`

---

## 11. 版本记录

### V1.00（2026-03-24）

1. 建立根 README 总入口。
2. 固化分层边界与依赖方向。
3. 补全当前任务配置清单。
4. 补全当前引脚与外设映射清单。
5. 增加后续维护者的同步更新提醒与版本治理规范。
