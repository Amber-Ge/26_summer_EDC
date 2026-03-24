/**
 * @file    mod_stepper.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   步进电机协议模块接口（TX Only）。
 * @details
 * 1. 文件作用：封装步进驱动协议组帧和发送链路，提供速度/位置/停机等命令接口。
 * 2. 解耦边界：本模块仅处理协议发送与上下文状态，不承载轨迹规划与视觉控制闭环。
 * 3. 上层绑定：`StepperTask` 负责轴通道编排、限位策略与控制节拍。
 * 4. 下层依赖：通过 `drv_uart` 发送字节流，并可绑定互斥锁保障多任务发送互斥。
 * 5. 生命周期：上下文需先 `ctx_init` 或 `bind`，运行期调用 `process` 维护发送状态。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_STEPPER_H
#define FINAL_GRADUATE_WORK_MOD_STEPPER_H

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
 * @brief 支持的 UART 实例数量（固定映射表长度）。
 *
 * 与 drv_uart / mod_uart_guard 保持一致：
 * - 0: USART1
 * - 1: USART2
 * - 2: USART3
 * - 3: UART4
 * - 4: UART5
 * - 5: USART6
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
    MOD_STEPPER_DIR_CW = 0U,    // 顺时针方向。发送位置/速度命令时 dir=0 表示该方向；机械正方向由接线与安装决定。
    MOD_STEPPER_DIR_CCW = 1U    // 逆时针方向。发送位置/速度命令时 dir=1 表示该方向，与 CW 相反。
} mod_stepper_dir_e;

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
    UART_HandleTypeDef *huart; // 本协议实例绑定的串口句柄（必填，不能为 NULL）。模块内所有 DMA 发送都通过该串口执行。
    osMutexId_t tx_mutex;      // 发送互斥锁（可选）。为 NULL 时表示不加锁；不为 NULL 时每次发送前先加锁，避免多任务并发写同一串口。
    uint8_t driver_addr;       // 协议目标驱动地址（必填，不能为 0）。该值会写入每个协议帧的第 1 字节用于选中目标驱动器。
} mod_stepper_bind_t;

/**
 * @brief 步进协议上下文（单实例运行时状态）。
 *
 * 一个 ctx 对应一个 UART + 一个 driver_addr。
 * 逻辑电机ID不放在此处，避免协议层与业务层耦合。
 */
typedef struct
{
    bool inited;                             // 上下文是否执行过初始化。ctx_init 成功后置 true；只有为 true 时才允许后续 bind/unbind/send。
    bool bound;                              // 上下文是否完成有效绑定。只有 inited=true 且 bound=true 才允许发送协议命令。
    mod_stepper_bind_t bind;                 // 当前生效的绑定参数副本。bind() 成功时写入；unbind() 时会清零。

    uint8_t tx_buf[MOD_STEPPER_TX_BUF_SIZE]; // DMA 发送暂存区。每次发送会先拷贝到此缓冲，避免 DMA 直接引用调用者临时内存。
    bool tx_active;                          // 当前是否存在“正在发送中的一帧”。DMA 启动成功后置 true；检测到发送完成或超时后清 false。
    uint32_t tx_active_tick;                 // 最近一次发送启动时刻（HAL_GetTick 计时基准）。用于超时判断与异常恢复。

    uint32_t tx_ok_count;                    // 发送成功计数。轮询检测到 UART 空闲并确认一次发送完成时自增，用于运行状态观测。
    uint32_t tx_fail_count;                  // 发送失败计数。DMA 启动失败或发送超时恢复时自增，用于定位链路稳定性问题。
} mod_stepper_ctx_t;

/* ========================= 生命周期与绑定 ========================= */

/**
 * @brief 初始化上下文，并可选立即绑定。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数；传 NULL 表示仅初始化，不绑定。
 * @return true 成功；false 参数非法或绑定失败。
 */
bool mod_stepper_ctx_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);

/**
 * @brief 兼容别名：与 mod_stepper_ctx_init 功能一致。
 */
bool mod_stepper_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);

/**
 * @brief 将上下文绑定到 UART + 驱动地址。
 * @param ctx 已初始化的上下文。
 * @param bind 绑定参数（huart 与 driver_addr 必填）。
 * @return true 绑定成功；false 绑定失败。
 */
bool mod_stepper_bind(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);

/**
 * @brief 解绑上下文并释放 UART 归属权。
 */
void mod_stepper_unbind(mod_stepper_ctx_t *ctx);

/**
 * @brief 查询上下文是否处于可工作状态。
 */
bool mod_stepper_is_bound(const mod_stepper_ctx_t *ctx);

/**
 * @brief 获取当前绑定配置只读指针。
 * @return 配置指针；若上下文非法/未初始化则返回 NULL。
 */
const mod_stepper_bind_t *mod_stepper_get_bind(const mod_stepper_ctx_t *ctx);

/**
 * @brief 轮询推进发送状态机。
 *
 * 使用方式：
 * - 传入具体 ctx：只处理该实例。
 * - 传入 NULL：轮询处理所有已注册实例。
 */
void mod_stepper_process(mod_stepper_ctx_t *ctx);

/* ========================= 原始发送与命令发送 ========================= */

/**
 * @brief 发送原始协议帧。
 * @param ctx 已绑定上下文。
 * @param buf 帧数据。
 * @param len 帧长度。
 * @return true DMA 启动成功；false 启动失败。
 */
bool mod_stepper_send_raw(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len);

/**
 * @brief 使能/失能命令。
 */
bool mod_stepper_enable(mod_stepper_ctx_t *ctx, bool enable, bool sync_flag);

/**
 * @brief 速度模式命令。
 */
bool mod_stepper_velocity(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);

/**
 * @brief 位置模式命令。
 */
bool mod_stepper_position(mod_stepper_ctx_t *ctx, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);

/**
 * @brief 立即停止命令。
 */
bool mod_stepper_stop(mod_stepper_ctx_t *ctx, bool sync_flag);

#endif /* FINAL_GRADUATE_WORK_MOD_STEPPER_H */


