/**
 * @file    mod_stepper.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   步进电机协议模块接口（TX Only）。
 * @details
 * 1. 文件作用：封装步进驱动协议组帧和发送链路，提供速度/位置/停机等命令接口。
 * 2. 解耦边界：本模块仅处理协议发送与上下文状态，不承载轨迹规划与视觉控制闭环。
 * 3. 上层绑定：具体轴通道编排、限位策略与控制节拍由任务层或装配层决定。
 * 4. 下层依赖：通过 `drv_uart` 发送字节流，并可绑定互斥锁保障多任务发送互斥。
 * 5. 生命周期：上下文需先 `ctx_init` 或 `bind`，运行期调用 `process` 维护发送状态。
 */
#ifndef ZGT6_FREERTOS_MOD_STEPPER_H
#define ZGT6_FREERTOS_MOD_STEPPER_H

#include "drv_uart.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 协议常量 ========================= */

/**
 * @brief EMM V5 协议固定帧尾字节。
 */
#define MOD_STEPPER_FRAME_TAIL               (0x6BU)

/**
 * @brief 速度上限（RPM）。
 *
 * 位置模式和速度模式都会使用该上限做限幅，超过后自动裁剪。
 */
#define MOD_STEPPER_VEL_MAX_RPM              (5000U)

/**
 * @brief 协议发送暂存缓冲大小（字节）。
 *
 * 精简后最长命令为位置模式（13字节），32字节留有足够冗余。
 */
#define MOD_STEPPER_TX_BUF_SIZE              (32U)

/**
 * @brief 支持的 UART 实例数量上限。
 *
 * 该值用于限制内部固定映射表大小，并与 `drv_uart` / `mod_uart_guard`
 * 的实例登记范围保持一致。它表示“最多可登记多少个 UART 实例”，
 * 不表示当前工程必须启用这些串口。
 */
#define MOD_STEPPER_MAX_UART_INSTANCES       (6U)

/**
 * @brief 发送活跃状态超时保护（毫秒）。
 *
 * 本模块不依赖 HAL_UART_TxCpltCallback 清理 tx_active，
 * 而是在 mod_stepper_process() 中轮询 UART 硬件状态；
 * 若超过该时间仍处于活跃，则触发超时恢复并累计失败计数。
 */
#define MOD_STEPPER_TX_ACTIVE_TIMEOUT_MS     (50U)

/**
 * @brief 发送互斥锁获取超时（毫秒）。
 *
 * 避免永久等待导致任务卡死。
 */
#define MOD_STEPPER_TX_MUTEX_TIMEOUT_MS       (5U)

/* ========================= 类型定义 ========================= */

/**
 * @brief 方向枚举（与协议字段保持一致）。
 */
typedef enum
{
    MOD_STEPPER_DIR_CW = 0U,
    MOD_STEPPER_DIR_CCW = 1U
} mod_stepper_dir_e;

typedef enum
{
    MOD_STEPPER_AXIS_1 = 0U,
    MOD_STEPPER_AXIS_2,
    MOD_STEPPER_AXIS_MAX
} mod_stepper_axis_e;

/**
 * @brief 协议层绑定配置。
 *
 * 字段说明：
 * - huart: 必填。协议发送依赖 UART DMA。
 * - tx_mutex: 可选。若不为 NULL，每次发送前会先获取互斥锁。
 * - driver_addr: 必填。作为协议帧第一个字节（目标驱动地址）。
 */
typedef struct
{
    UART_HandleTypeDef *huart;
    osMutexId_t tx_mutex;
    uint8_t driver_addr;
} mod_stepper_bind_t;

/**
 * @brief 步进协议上下文（单实例运行时状态）。
 *
 * 一个 ctx 对应一个 UART + 一个 driver_addr。
 * 逻辑电机ID不放在此处，避免协议层与业务层耦合。
 */
typedef struct
{
    bool inited;
    bool bound;
    mod_stepper_bind_t bind;

    uint8_t tx_buf[MOD_STEPPER_TX_BUF_SIZE];
    bool tx_active;
    uint32_t tx_active_tick;

    uint32_t tx_ok_count;
    uint32_t tx_fail_count;
} mod_stepper_ctx_t;

/* ========================= 生命周期与绑定 ========================= */

mod_stepper_ctx_t *mod_stepper_get_default_ctx(mod_stepper_axis_e axis);
bool mod_stepper_ctx_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);
void mod_stepper_ctx_deinit(mod_stepper_ctx_t *ctx);
bool mod_stepper_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);
bool mod_stepper_bind(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);
void mod_stepper_unbind(mod_stepper_ctx_t *ctx);
bool mod_stepper_is_bound(const mod_stepper_ctx_t *ctx);
const mod_stepper_bind_t *mod_stepper_get_bind(const mod_stepper_ctx_t *ctx);
void mod_stepper_process(mod_stepper_ctx_t *ctx);

/* ========================= 原始发送与命令发送 ========================= */

bool mod_stepper_send_raw(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len);
bool mod_stepper_enable(mod_stepper_ctx_t *ctx, bool enable, bool sync_flag);
bool mod_stepper_velocity(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);
bool mod_stepper_position(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);
bool mod_stepper_stop(mod_stepper_ctx_t *ctx, bool sync_flag);

#endif /* ZGT6_FREERTOS_MOD_STEPPER_H */
