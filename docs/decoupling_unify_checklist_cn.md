/**
 * @file    decoupling_unify_checklist_cn.md
 * @brief   工程解耦与初始化一致性检查清单（第一版）
 * @details
 * 1. 文件作用：记录当前工程在 Module/Driver 层的解耦差异与统一改造进展。
 * 2. 上下层绑定：供任务层、模块层、驱动层统一接口时参考，不参与编译。
 */

# 解耦一致性检查（Module / Driver）

## 1. `ctx` 风格使用现状
- `ctx` 风格：`mod_k230`、`mod_stepper`、`mod_vofa`。
- 静态单例/全局状态风格：`mod_battery`、`mod_key`、`mod_led`、`mod_motor`、`mod_oled`、`mod_relay`、`mod_sensor`、`mod_uart_guard`。
- 结论：当前是混合风格，调用者的心智模型不完全统一。

## 2. `init` 接口覆盖现状
- `Module` 层具备 `init` 入口：`mod_battery_init`、`mod_key_init`、`mod_led_init`、`mod_motor_init`、`mod_oled_init`、`mod_relay_init`、`mod_sensor_init`、`mod_stepper_init`、`mod_uart_guard_init`、`mod_vofa_init`、`mod_k230_init`。
- `Module` 层特殊项：`mod_oled_data` 为字库数据文件，不承担独立生命周期，无 `init` 合理。
- `Driver` 层具备 `init/device_init`：`drv_encoder_device_init`、`drv_key_init`、`drv_pwm_device_init`、`drv_uart_init`。
- `Driver` 层当前无 `init`：`drv_adc`、`drv_gpio`（当前为轻量无状态驱动，可保留无 `init` 设计）。

## 3. 命名一致性现状
- 旧命名与新命名并存：例如 `OLED_*` 与 `mod_oled_*`。
- 统一策略：新增统一命名接口，旧接口保留为兼容包装，逐步迁移调用点。

## 4. 第一批已落地改造（兼容式）
- 补齐 `drv_uart_init` 实现，统一驱动层初始化入口。
- 新增 `mod_led_init`，旧 `mod_led_Init` 保留并转调新接口。
- 新增 `mod_oled_init`，内部转调旧 `OLED_Init`。
- 新增 `mod_battery_init`，用于复位模块运行态缓存。
- 新增 `mod_uart_guard_init`，用于清空 UART 归属状态表。
- `task_init.c` 启动阶段改为统一调用 `drv_uart_init` 与 `mod_uart_guard_init`。

## 5. 第二批已落地改造（兼容式）
- `mod_oled.h/.c` 新增统一命名包装接口：`mod_oled_bind_i2c`、`mod_oled_unbind_i2c`、`mod_oled_is_bound_i2c`、`mod_oled_update`、`mod_oled_clear`、`mod_oled_show_string`、`mod_oled_show_num`、`mod_oled_show_float_num`。
- `task_oled.c` 任务层调用已从旧 `OLED_*` 切换到 `mod_oled_*`。
- `mod_k230.h/.c` 新增默认上下文统一入口：`mod_k230_init`、`mod_k230_deinit`。
- `task_init.c` 的 K230 绑定逻辑改为统一入口 `mod_k230_init(&k230_bind)`。
- 新增中文注释模板文件：`Code/03_Common/Inc/comment_template_cn.h`（UTF-8 无乱码，覆盖文件头、结构体、函数、步骤注释规范）。

## 6. 后续建议（第三批）
- 继续把任务层中直接依赖“旧命名接口”的历史代码迁移到 `mod_*` 统一入口。
- 给仍是“静态单例风格”的模块补充可选 `deinit`，形成 `init -> bind -> run -> unbind/deinit` 生命周期闭环。
- 评估是否给 `drv_adc` / `drv_gpio` 提供空实现 `init`，使上层初始化脚本模板完全一致。
- 在 `Drv/Module` 层 `.h` 文件持续保持 `@author` 与 `@version`，其余层只保留作用说明与上下层绑定说明。

## 7. 第三批已落地改造（进行中）
- `mod_vofa.h` 已重写为无乱码中文注释，并补齐默认上下文生命周期接口声明：`mod_vofa_ctx_deinit`、`mod_vofa_deinit`。
- `mod_vofa.c` 已新增 `mod_vofa_ctx_deinit` 与 `mod_vofa_deinit` 实现，形成默认上下文 `init -> bind -> run -> deinit` 闭环。
- `mod_vofa.c` 关键函数注释与变量注释已清理为无乱码中文（接收回调、绑定流程、发送流程、默认包装接口）。
- `task_init.c` 历史乱码注释已清理为无乱码中文，初始化任务职责说明已统一。
- `task_stepper.c` 文件头注释已改为具体职责说明，补齐与 `mod_stepper/mod_k230/mod_vofa` 的上下层绑定描述。
