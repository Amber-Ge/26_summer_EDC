/**
 * @file    mod_k230.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-23
 * @brief   K230 协议模块接口（绑定驱动版）。
 * @details
 * 1. 文件作用：封装 K230 协议帧收发、解析、缓冲与上下文状态管理。
 * 2. 解耦边界：协议格式与收发状态在本模块内闭环；不承载视觉业务决策与运动控制逻辑。
 * 3. 上层绑定：`InitTask` 注入串口与同步资源，`StepperTask` 周期拉取最新有效帧。
 * 4. 下层依赖：通过 `drv_uart` 收发字节流，可选绑定互斥锁/信号量实现多任务协作。
 * 5. 生命周期：默认上下文需先 `ctx_init/bind`，运行期调用 `process` 推进协议状态机。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_K230_H
#define FINAL_GRADUATE_WORK_MOD_K230_H

#include "drv_uart.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 资源规模配置 ========================= */
/* DMA 一次性接收缓冲。该缓冲会在 RX 回调中批量搬运到软件环形缓冲。 */
#define MOD_K230_RX_DMA_BUF_SIZE      (256U)
/* 协议层软件环形缓冲。用于吸收上层处理速度抖动。 */
#define MOD_K230_RX_RING_BUF_SIZE     (1024U)
/* 发送缓冲。避免 DMA 直接引用调用者栈内存。 */
#define MOD_K230_TX_BUF_SIZE          (512U)
/* 允许绑定的最大事件信号量数量。 */
#define MOD_K230_MAX_BIND_SEM         (4U)
/* 发送互斥锁的获取超时（毫秒）。 */
#define MOD_K230_TX_MUTEX_TIMEOUT_MS  (5U)
/* K230 当前固定协议帧长度（字节）。 */
#define MOD_K230_PROTO_FRAME_SIZE     (12U)
/* 单次获取最新帧时最多处理的 64B 批次数，防止任务长时间占用 CPU */
#define MOD_K230_MAX_READ_BATCH_PER_CALL (6U)

/**
 * @brief K230 帧校验算法枚举。
 * @details
 * 当前仅实现 XOR，但接口层已开放“可选算法”语义，
 * 后续扩展 CRC8/CRC16 时无需再改动 bind 架构。
 */
typedef enum
{
    MOD_K230_CHECKSUM_XOR = 0U, // XOR 校验算法。当前工程唯一已实现且已验证的算法类型。
    MOD_K230_CHECKSUM_MAX
} mod_k230_checksum_algo_t;

/**
 * @brief K230 协议帧解码结果。
 */
typedef struct
{
    uint8_t motor1_id; // 协议帧中的电机1逻辑ID。任务层用它决定把 err1 分发给哪个电机通道。
    int16_t err1;      // 协议帧中的电机1误差值（大端高字节在前）。正负号含义由任务层方向映射策略解释。
    uint8_t motor2_id; // 协议帧中的电机2逻辑ID。任务层用它决定把 err2 分发给哪个电机通道。
    int16_t err2;      // 协议帧中的电机2误差值（大端高字节在前）。一般与 motor2_id 配对使用。
} mod_k230_frame_data_t;

/**
 * @brief K230 绑定参数（硬件+OS 资源+校验策略）。
 * @details
 * 这是“协议层输入契约”：
 * - huart / tx_mutex / sem_list 来源于系统装配层（例如 task_init）
 * - checksum_algo 决定帧合法性判定方式
 */
typedef struct
{
    UART_HandleTypeDef *huart;                         // 绑定的串口句柄（必填，不能为空）。K230 收发 DMA 都依赖此句柄。
    osSemaphoreId_t sem_list[MOD_K230_MAX_BIND_SEM];  // 数据到达通知信号量列表。收到一批新数据后会逐个 release。
    uint8_t sem_count;                                 // sem_list 实际有效数量。只会读取 [0, sem_count) 范围内的元素。
    osMutexId_t tx_mutex;                              // 发送互斥锁（可选）。为 NULL 表示发送不加锁；非 NULL 表示发送前先加锁。
    mod_k230_checksum_algo_t checksum_algo;            // 接收帧校验算法选择。决定解析时用哪种算法判定帧合法性。
} mod_k230_bind_t;

/**
 * @brief K230 运行上下文（每个上下文对应一个协议实例）。
 * @details
 * 该结构体既包含绑定信息，也包含协议运行态缓存；
 * 如需多实例，可创建多个 ctx，并分别绑定不同 UART。
 */
typedef struct
{
    bool inited;                                       // 上下文是否已初始化。ctx_init 成功后为 true，未初始化不允许 bind/send/read。
    bool bound;                                        // 上下文是否已绑定完成。true 代表 UART、回调、DMA 接收链路已建立。
    mod_k230_bind_t bind;                              // 当前生效绑定参数副本。bind 成功写入，unbind/deinit 时会清理。

    uint8_t rx_dma_buf[MOD_K230_RX_DMA_BUF_SIZE];      // DMA 接收临时缓冲。UART DMA 回调上报的数据先落到这里，再搬运到环形缓冲。
    uint8_t rx_ring_buf[MOD_K230_RX_RING_BUF_SIZE];    // 软件环形接收缓冲。用于缓存待解析数据，吸收任务调度抖动与突发数据。
    volatile uint16_t rx_head;                         // 环形缓冲写指针。写入新字节时前移，可能在中断上下文更新，因此声明为 volatile。
    volatile uint16_t rx_tail;                         // 环形缓冲读指针。上层读取/解析时前移，表示已消费到的位置。

    uint8_t tx_buf[MOD_K230_TX_BUF_SIZE];              // DMA 发送缓冲。发送前先复制用户数据，避免 DMA 引用调用者短生命周期内存。

    uint8_t parse_buf[MOD_K230_PROTO_FRAME_SIZE];      // 协议解析状态机缓存。按字节流逐步拼出固定 12 字节帧。
    uint8_t parse_len;                                 // 当前 parse_buf 已填充字节数。用于表示解析状态机进度。
} mod_k230_ctx_t;

/* ========================= Context 生命周期 ========================= */

/**
 * @brief 获取模块内置默认上下文。
 * @return mod_k230_ctx_t* 默认上下文地址。
 */
mod_k230_ctx_t *mod_k230_get_default_ctx(void);

/**
 * @brief 初始化上下文；可选同时完成绑定。
 * @param ctx 目标上下文。
 * @param bind 可选绑定参数；传 NULL 表示仅初始化，不立即绑定。
 * @return true 成功。
 * @return false 失败（参数非法或绑定失败）。
 */
bool mod_k230_ctx_init(mod_k230_ctx_t *ctx, const mod_k230_bind_t *bind);

/**
 * @brief 反初始化上下文并释放绑定资源。
 * @param ctx 目标上下文。
 */
void mod_k230_ctx_deinit(mod_k230_ctx_t *ctx);

/**
 * @brief 将上下文绑定到指定硬件与策略。
 * @details
 * 若 ctx 当前已绑定，会先执行 unbind，再按新 bind 重新建立。
 *
 * @param ctx 目标上下文。
 * @param bind 绑定参数（huart 必填，算法必须受支持）。
 * @return true 绑定成功。
 * @return false 绑定失败。
 */
bool mod_k230_bind(mod_k230_ctx_t *ctx, const mod_k230_bind_t *bind);

/**
 * @brief 解除上下文绑定，停止 DMA 并释放 UART 归属权。
 * @param ctx 目标上下文。
 */
void mod_k230_unbind(mod_k230_ctx_t *ctx);

/**
 * @brief 查询上下文是否处于可工作状态（inited + bound + huart 有效）。
 * @param ctx 目标上下文。
 * @return true 已就绪。
 * @return false 未就绪。
 */
bool mod_k230_is_bound(const mod_k230_ctx_t *ctx);

/* ========================= 绑定配置动态调整 ========================= */

/**
 * @brief 动态切换帧校验算法。
 * @param ctx 目标上下文。
 * @param algo 目标算法。
 * @return true 切换成功。
 * @return false 参数非法或算法不支持。
 */
bool mod_k230_set_checksum_algo(mod_k230_ctx_t *ctx, mod_k230_checksum_algo_t algo);

/**
 * @brief 追加一个数据到达通知信号量。
 * @param ctx 目标上下文。
 * @param sem_id 待绑定信号量。
 * @return true 追加成功（若已存在也返回 true）。
 * @return false 追加失败。
 */
bool mod_k230_add_semaphore(mod_k230_ctx_t *ctx, osSemaphoreId_t sem_id);

/**
 * @brief 移除一个数据到达通知信号量。
 * @param ctx 目标上下文。
 * @param sem_id 待移除信号量。
 * @return true 移除成功。
 * @return false 移除失败。
 */
bool mod_k230_remove_semaphore(mod_k230_ctx_t *ctx, osSemaphoreId_t sem_id);

/**
 * @brief 清空所有通知信号量绑定。
 * @param ctx 目标上下文。
 */
void mod_k230_clear_semaphores(mod_k230_ctx_t *ctx);

/**
 * @brief 设置发送互斥锁。
 * @param ctx 目标上下文。
 * @param mutex_id 互斥锁句柄；NULL 表示关闭互斥保护。
 */
void mod_k230_set_tx_mutex(mod_k230_ctx_t *ctx, osMutexId_t mutex_id);

/* ========================= 收发与解析接口 ========================= */

/**
 * @brief 发送原始字节流（DMA）。
 * @param ctx 目标上下文。
 * @param data 数据首地址。
 * @param len 数据长度（字节）。
 * @return true DMA 启动成功。
 * @return false 参数非法、通道忙或上下文未就绪。
 */
bool mod_k230_send_bytes(mod_k230_ctx_t *ctx, const uint8_t *data, uint16_t len);

/**
 * @brief 查询 TX 通道是否空闲。
 * @param ctx 目标上下文。
 * @return true 空闲。
 * @return false 忙或上下文未就绪。
 */
bool mod_k230_is_tx_free(const mod_k230_ctx_t *ctx);

/**
 * @brief 查询接收环形缓冲当前可读字节数。
 * @param ctx 目标上下文。
 * @return uint16_t 可读字节数。
 */
uint16_t mod_k230_available(const mod_k230_ctx_t *ctx);

/**
 * @brief 从接收环形缓冲读取数据。
 * @param ctx 目标上下文。
 * @param out 输出缓冲。
 * @param max_len 最大读取长度。
 * @return uint16_t 实际读取长度。
 */
uint16_t mod_k230_read_bytes(mod_k230_ctx_t *ctx, uint8_t *out, uint16_t max_len);

/**
 * @brief 清空接收环形缓冲与解析状态机。
 * @param ctx 目标上下文。
 */
void mod_k230_clear_rx_buffer(mod_k230_ctx_t *ctx);

/**
 * @brief 从接收流中解析“最新一帧有效协议帧”。
 * @details
 * 函数会尽可能消费当前缓冲中的全部字节，最终输出最后一次解析成功的帧。
 *
 * @param ctx 目标上下文。
 * @param out_frame 输出结构体。
 * @return true 解析到至少一帧有效帧。
 * @return false 未解析到有效帧或参数非法。
 */
bool mod_k230_get_latest_frame(mod_k230_ctx_t *ctx, mod_k230_frame_data_t *out_frame);

#endif /* FINAL_GRADUATE_WORK_MOD_K230_H */


