# Common 层开发与维护详解

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Common Layer Guide |
| 所在层 | `Code/03_Common` |
| 作者 | 姜凯中（工程原作者） / Codex（文档整理与体系化说明） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |
| 读者对象 | 控制算法开发者、协议开发者、任务层维护人员 |

---

## 1. 层定位与边界

Common 层是“纯算法与通用工具层”，目标是把可复用逻辑从业务代码中剥离出来。

Common 层负责：

1. 通用算法与数学工具（PID、校验、字符串转换）。
2. 编译期参数集中管理（`pid_config.h`）。
3. 与硬件无关的基础能力复用。

Common 层不负责：

1. 任务调度、状态机逻辑。
2. 设备访问、协议收发、HAL 调用。

---

## 2. 目录与组件总览

| 子目录/文件 | 作用 | 主要使用方 |
| --- | --- | --- |
| `Inc/common_checksum.h` + `Src/common_checksum.c` | XOR 校验 | `mod_k230` |
| `Inc/common_str.h` + `Src/common_str.c` | 数值转字符串 | `mod_vofa` |
| `PID/pid_config.h` | 控制参数默认值集中定义 | `task_dcc`, PID 组件 |
| `PID/pid_inc.h/.c` | 增量式 PID | `task_dcc`, `pid_multi` |
| `PID/pid_pos.h/.c` | 位置式 PID | `task_dcc`, `task_stepper`, `pid_multi` |
| `PID/pid_multi.h/.c` | 串级 PID 组合 | 可供后续控制任务扩展 |

---

## 3. `common_checksum` 详解

接口：

1. `common_checksum_xor_u8(const uint8_t *data, uint16_t len)`

行为：

1. 对输入字节序列逐字节异或。
2. 空指针或长度为 0 时返回 `0`。

当前使用场景：

1. `mod_k230` 协议帧校验字段计算。

扩展建议：

1. 后续新增 CRC8/CRC16 时可在同层新增 `common_checksum_crc8/crc16`，保持调用层不依赖具体算法实现细节。

---

## 4. `common_str` 详解

接口：

1. `common_uint_to_str`
2. `common_int_to_str`
3. `common_float_to_str`

设计特点：

1. 全部带缓冲区长度参数，防越界。
2. 浮点转换精度由 `COMMON_STR_FLOAT_PRECISION` 控制。
3. 内部使用工具函数 `_u32_to_str_tool`，减少重复逻辑。

当前使用场景：

1. `mod_vofa` 组包发送 `float/int/uint` 数组文本。

维护建议：

1. 不要在该层引入 `sprintf` 依赖，保持嵌入式可控性。
2. 若增大浮点精度，需评估发送带宽和缓冲区占用。

---

## 5. PID 体系总体设计

PID 组件由三部分组成：

1. `pid_pos`：位置式 PID，适合绝对误差闭环。
2. `pid_inc`：增量式 PID，适合速度等增量调节场景。
3. `pid_multi`：外环+内环组合框架，支持算法可切换。

核心解耦点：

1. PID 仅做数学计算，不直接访问电机或传感器。
2. 具体采样周期由调用方保证，PID 不持有时间基准。
3. 目标值设置与输出执行完全由上层任务管理。

---

## 6. `pid_config.h` 参数管理

主要参数：

1. `MOTOR_TARGET_SPEED`、`MOTOR_TARGET_ERROR`
2. 位置环：`MOTOR_POS_KP/KI/KD/OUTPUT_MAX/INTEGRAL_MAX`
3. 速度环：`MOTOR_SPEED_KP/KI/KD`

管理原则：

1. 所有“默认参数”集中定义，避免散落在任务代码中。
2. 改参后需记录“为什么改、影响哪条控制链路”。
3. 涉及安全边界参数（输出限幅）必须配套回归测试。

---

## 7. `pid_inc`（增量式 PID）详解

关键接口：

1. `PID_Inc_Init`
2. `PID_Inc_SetTarget`
3. `PID_Inc_Compute`
4. `PID_Inc_Reset`

计算公式：

1. `delta_u = Kp*(e(k)-e(k-1)) + Ki*e(k) + Kd*(e(k)-2e(k-1)+e(k-2))`
2. `u(k) = u(k-1) + delta_u`

实现要点：

1. 内部维护 `error/last_error/prev_error`。
2. 输出有上下限，默认对称。

适用场景：

1. 速度环控制（当前用于 DCC 左右轮速度环）。

---

## 8. `pid_pos`（位置式 PID）详解

关键接口：

1. `PID_Pos_Init`
2. `PID_Pos_SetTarget`
3. `PID_Pos_SetOutputLimit`
4. `PID_Pos_SetIntegralLimit`
5. `PID_Pos_Compute`
6. `PID_Pos_Reset`

计算结构：

1. `error = target - measure`
2. `integral += error`（并限幅）
3. `p_term = kp*error`
4. `i_term = ki*integral`
5. `d_term = kd*(error-last_error)`
6. `output = p+i+d`（并限幅）

实现要点：

1. 内置积分限幅与输出限幅，抑制饱和。
2. 暴露 `p/i/d` 分量，便于调试可观测。

适用场景：

1. 位置差纠偏（DCC 直线模式）。
2. 视觉误差转位置命令（Stepper 任务）。

---

## 9. `pid_multi`（串级 PID）详解

关键接口：

1. `PID_Multi_Init`
2. `PID_Multi_SetCascadeEnable`
3. `PID_Multi_SetOuterAlgo`
4. `PID_Multi_SetInnerAlgo`
5. `PID_Multi_SetInnerTargetLimit`
6. `PID_Multi_Compute`

运行模式：

1. 串级开启：外环输出作为内环目标。
2. 串级关闭：总目标直接进入内环。

扩展能力：

1. 外环可选位置式或增量式。
2. 内环可选位置式或增量式。
3. 内环目标可限幅，防止外环过激输出。

当前状态：

1. 已实现并可复用。
2. 当前工程主链路暂未大规模使用 `pid_multi`，属于可扩展能力储备。

---

## 10. Common 层调用关系

1. `task_dcc` -> `PID_Pos/PID_Inc`
2. `task_stepper` -> `PID_Pos`
3. `mod_k230` -> `common_checksum`
4. `mod_vofa` -> `common_str`
5. `pid_multi` -> `pid_pos + pid_inc`

---

## 11. 扩展指南（Common 层）

新增通用工具函数建议：

1. 接口必须无硬件依赖。
2. 必须提供参数边界检查。
3. 尽量避免动态内存分配。

新增控制算法建议：

1. 先定义独立头文件与对象结构体。
2. 暴露 `Init/SetTarget/Compute/Reset` 统一接口风格。
3. 默认参数统一接入 `pid_config.h` 或同类配置文件。

---

## 12. 维护与回归建议

维护检查：

1. 参数改动是否同步更新文档。
2. 输出限幅和积分限幅是否仍满足安全边界。
3. 算法接口是否保持向后兼容。

建议回归用例：

1. `PID_Pos`：阶跃输入观察超调与稳态误差。
2. `PID_Inc`：速度目标突变时输出变化是否平滑。
3. `common_str`：极值、负值、零值、缓冲区边界。
4. `common_checksum`：已知向量校验值一致性。

---

## 13. 常见问题排查

1. 控制输出振荡：
   - 先看是否调用周期变化，再看 `kp/ki/kd`，最后看传感器噪声。
2. 输出长时间饱和：
   - 检查 `out_max/integral_max` 是否过小或目标不可达。
3. VOFA 数值显示异常：
   - 检查 `common_float_to_str` 精度配置和发送缓冲长度。
4. K230 校验失败：
   - 检查协议字段范围和 `common_checksum_xor_u8` 输入区间。

---

## 14. 版本演进建议

1. Common 层新增函数时必须补充“调用场景”和“边界语义”。
2. PID 参数体系如改名，需提供迁移表，避免任务层参数断链。
3. 若未来引入定点算法，建议新增并行模块，保持浮点版本可回退。
