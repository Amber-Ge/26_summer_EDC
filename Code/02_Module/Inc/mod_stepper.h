/**
 ******************************************************************************
 * @file    mod_stepper.h
 * @brief   步进电机通信模块接口定义
 * @details
 * 提供步进驱动器协议通信、DMA 收发、状态维护与常用控制命令接口。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_STEPPER_H
#define FINAL_GRADUATE_WORK_MOD_STEPPER_H // 头文件防重复包含宏

#include "drv_uart.h"
#include "cmsis_os2.h"
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MOD_STEPPER_TX_BUF_SIZE              (32U) // 发送缓冲区大小，单位字节
#define MOD_STEPPER_RX_DMA_BUF_SIZE          (192U) // DMA接收缓冲区大小，单位字节
#define MOD_STEPPER_RX_FRAME_BUF_SIZE        (192U) // 单帧最大缓存大小，单位字节
#define MOD_STEPPER_RX_RING_SIZE             (8U) // 接收帧环形队列深度
#define MOD_STEPPER_FRAME_TAIL               (0x6BU) // 协议帧尾标志
#define MOD_STEPPER_VEL_MAX_RPM              (5000U) // 速度模式最大转速，单位 RPM
#define MOD_STEPPER_TX_QUEUE_SIZE            (16U) // 发送队列深度
#define MOD_STEPPER_MAX_UART_INSTANCES       (6U) // 支持的串口实例数量上限
#define MOD_STEPPER_LOGIC_ID_MAX             (255U) // 逻辑ID最大值
#define MOD_STEPPER_TX_ACTIVE_TIMEOUT_MS     (50U) // 发送活跃超时，单位 ms

#define MOD_STEPPER_RX_BUF_SIZE              MOD_STEPPER_RX_DMA_BUF_SIZE // 兼容别名：接收DMA缓冲区大小
#define MOD_STEPPER_FRAME_BUF_SIZE           MOD_STEPPER_RX_FRAME_BUF_SIZE // 兼容别名：单帧缓存大小
#define MOD_STEPPER_MAX_MOTORS               (1U) // 单上下文支持的电机数量

typedef enum
{
    MOD_STEPPER_DIR_CW = 0U, // 顺时针方向
    MOD_STEPPER_DIR_CCW = 1U // 逆时针方向
} mod_stepper_dir_e;

typedef enum
{
    MOD_STEPPER_ORIGIN_MODE_NEAREST = 0U, // 就近回零
    MOD_STEPPER_ORIGIN_MODE_SINGLE = 1U, // 单方向回零
    MOD_STEPPER_ORIGIN_MODE_LIMIT = 2U, // 限位触发回零
    MOD_STEPPER_ORIGIN_MODE_LIMIT_BACK = 3U // 限位回退回零
} mod_stepper_origin_mode_e;

typedef enum
{
    MOD_STEPPER_SYS_VBUS = 5, // 母线电压
    MOD_STEPPER_SYS_CBUS = 6, // 母线电流
    MOD_STEPPER_SYS_CPHA = 7, // 相电流
    MOD_STEPPER_SYS_ENCO = 8, // 编码器当前位置
    MOD_STEPPER_SYS_CLKC = 9, // 堵转相关计数
    MOD_STEPPER_SYS_ENCL = 10, // 编码器锁定值
    MOD_STEPPER_SYS_CLKI = 11, // 堵转积分项
    MOD_STEPPER_SYS_TPOS = 12, // 目标位置
    MOD_STEPPER_SYS_SPOS = 13, // 设定位置
    MOD_STEPPER_SYS_VEL = 14, // 实际速度
    MOD_STEPPER_SYS_CPOS = 15, // 当前位置
    MOD_STEPPER_SYS_PERR = 16, // 位置误差
    MOD_STEPPER_SYS_VBAT = 17, // 供电电压
    MOD_STEPPER_SYS_TEMP = 18, // 温度
    MOD_STEPPER_SYS_FLAG = 19, // 状态标志位
    MOD_STEPPER_SYS_OFLAG = 20, // 回零结果标志
    MOD_STEPPER_SYS_OAF = 21, // 回零附加标志
    MOD_STEPPER_SYS_PIN = 22 // IO输入状态
} mod_stepper_sys_param_e;

typedef struct
{
    UART_HandleTypeDef *huart; // 绑定的串口句柄
    osSemaphoreId_t sem_id; // 串口接收信号量
    uint8_t motor_id; // 逻辑电机ID
    uint8_t driver_addr; // 目标驱动器地址
} mod_stepper_bind_t;

typedef struct
{
    uint16_t len; // 帧数据长度
    uint8_t data[MOD_STEPPER_RX_FRAME_BUF_SIZE]; // 帧数据缓存
    uint32_t tick; // 帧入队时系统tick
} mod_stepper_rx_frame_t;

typedef struct
{
    uint16_t len; // 待发送数据长度
    uint8_t data[MOD_STEPPER_TX_BUF_SIZE]; // 待发送数据缓存
} mod_stepper_tx_node_t;

typedef struct
{
    bool inited; // 上下文是否已初始化
    bool registered; // 当前地址是否已注册
    mod_stepper_bind_t bind; // 绑定参数

    uint8_t rx_dma_buf[MOD_STEPPER_RX_DMA_BUF_SIZE]; // DMA接收缓冲区
    mod_stepper_rx_frame_t rx_ring[MOD_STEPPER_RX_RING_SIZE]; // 接收帧环形队列
    uint16_t rx_head; // 接收队列写索引
    uint16_t rx_tail; // 接收队列读索引
    uint16_t rx_count; // 接收队列有效帧数

    uint8_t last_frame[MOD_STEPPER_RX_FRAME_BUF_SIZE]; // 最近一帧缓存
    uint16_t last_frame_len; // 最近一帧长度
    bool has_new_frame; // 是否有新帧未读取

    uint8_t last_ack_cmd; // 最近ACK命令字
    uint8_t last_ack_status; // 最近ACK状态码
    bool has_new_ack; // 是否有新ACK未读取
    uint32_t last_rx_tick; // 最近接收时间戳

    mod_stepper_tx_node_t tx_queue[MOD_STEPPER_TX_QUEUE_SIZE]; // 发送队列
    uint16_t tx_q_head; // 发送队列写索引
    uint16_t tx_q_tail; // 发送队列读索引
    uint16_t tx_q_count; // 发送队列有效节点数
    volatile bool tx_active; // 当前是否在发送中
    uint8_t tx_active_buf[MOD_STEPPER_TX_BUF_SIZE]; // 当前发送缓冲
    uint32_t tx_active_tick; // 当前发送启动时间戳

    uint32_t rx_push_count; // 接收入队计数
    uint32_t rx_drop_count; // 接收丢帧计数
    uint32_t tx_push_count; // 发送入队计数
    uint32_t tx_drop_count; // 发送丢包计数
    uint32_t tx_fail_count; // 发送失败计数
} mod_stepper_ctx_t;

/** @brief 初始化步进模块上下文并可选绑定 UART。 */
bool mod_stepper_init(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);
/** @brief 绑定 UART 与地址配置。 */
bool mod_stepper_bind(mod_stepper_ctx_t *ctx, const mod_stepper_bind_t *bind);
/** @brief 解绑当前上下文。 */
void mod_stepper_unbind(mod_stepper_ctx_t *ctx);
/** @brief 获取当前绑定配置。 */
const mod_stepper_bind_t *mod_stepper_get_bind(const mod_stepper_ctx_t *ctx);

/** @brief 启动 DMA 接收。 */
bool mod_stepper_start_dma_rx(mod_stepper_ctx_t *ctx);
/** @brief 停止 DMA 接收。 */
void mod_stepper_stop_dma_rx(mod_stepper_ctx_t *ctx);
/** @brief 查询上下文是否处于已绑定状态。 */
bool mod_stepper_is_bound(const mod_stepper_ctx_t *ctx);

/** @brief 执行模块周期处理。 */
void mod_stepper_process(mod_stepper_ctx_t *ctx);

/** @brief 注册电机地址。 */
bool mod_stepper_register_motor(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 注销电机地址。 */
bool mod_stepper_unregister_motor(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 获取已注册电机数量。 */
uint8_t mod_stepper_get_registered_motor_count(const mod_stepper_ctx_t *ctx);
/** @brief 查询电机是否在线。 */
bool mod_stepper_is_motor_online(const mod_stepper_ctx_t *ctx, uint8_t addr, uint32_t timeout_ms);
/** @brief 设置驱动地址映射。 */
bool mod_stepper_set_driver_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t driver_addr);
/** @brief 获取驱动地址映射。 */
uint8_t mod_stepper_get_driver_addr(const mod_stepper_ctx_t *ctx);

/** @brief 发送原始协议数据。 */
bool mod_stepper_send_raw(mod_stepper_ctx_t *ctx, const uint8_t *buf, uint16_t len);
/** @brief 触发编码器校准。 */
bool mod_stepper_trigger_encoder_cal(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 复位电机。 */
bool mod_stepper_reset_motor(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 清除堵转保护。 */
bool mod_stepper_reset_clog_pro(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 恢复出厂/默认参数。 */
bool mod_stepper_restore_motor(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 修改单台电机 ID。 */
bool mod_stepper_modify_motor_id_single(mod_stepper_ctx_t *ctx, uint8_t old_addr, bool save_flag, uint8_t new_addr);
/** @brief 使能/失能电机。 */
bool mod_stepper_enable(mod_stepper_ctx_t *ctx, uint8_t addr, bool enable, bool sync_flag);
/** @brief 速度模式运行。 */
bool mod_stepper_velocity(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, bool sync_flag);
/** @brief 位置模式运行。 */
bool mod_stepper_position(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_dir_e dir, uint16_t vel_rpm, uint8_t acc, uint32_t pulse, bool absolute_mode, bool sync_flag);
/** @brief 停止运行。 */
bool mod_stepper_stop(mod_stepper_ctx_t *ctx, uint8_t addr, bool sync_flag);
/** @brief 触发同步起动。 */
bool mod_stepper_sync_start(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 当前坐标清零。 */
bool mod_stepper_reset_cur_pos_to_zero(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 回零坐标清零。 */
bool mod_stepper_origin_set_zero(mod_stepper_ctx_t *ctx, uint8_t addr, bool save_flag);
/** @brief 触发回零流程。 */
bool mod_stepper_origin_trigger(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_origin_mode_e mode, bool sync_flag);
/** @brief 中断回零流程。 */
bool mod_stepper_origin_interrupt(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 读取回零参数。 */
bool mod_stepper_origin_read_params(mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 修改回零参数。 */
bool mod_stepper_origin_modify_params(mod_stepper_ctx_t *ctx, uint8_t addr, bool save_flag, mod_stepper_origin_mode_e mode, mod_stepper_dir_e dir, uint16_t origin_vel_rpm, uint32_t origin_timeout_ms, uint16_t collide_vel_rpm, uint16_t collide_current_ma, uint16_t collide_time_ms, bool power_on_trigger);
/** @brief 读取系统参数。 */
bool mod_stepper_read_sys_param(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_sys_param_e param);
/** @brief 设置系统参数自动回传周期。 */
bool mod_stepper_auto_return_sys_param_timed(mod_stepper_ctx_t *ctx, uint8_t addr, mod_stepper_sys_param_e param, uint16_t period_ms);

/** @brief 判断是否有新帧。 */
bool mod_stepper_has_new_frame(const mod_stepper_ctx_t *ctx);
/** @brief 读取最后一帧数据。 */
bool mod_stepper_get_last_frame(mod_stepper_ctx_t *ctx, uint8_t *out_buf, uint16_t *inout_len);
/** @brief 读取最后 ACK。 */
bool mod_stepper_get_last_ack(mod_stepper_ctx_t *ctx, uint8_t *cmd, uint8_t *status);
/** @brief 按地址判断是否有新帧。 */
bool mod_stepper_has_new_frame_by_addr(const mod_stepper_ctx_t *ctx, uint8_t addr);
/** @brief 按地址读取最后一帧。 */
bool mod_stepper_get_last_frame_by_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t *out_buf, uint16_t *inout_len);
/** @brief 按地址读取最后 ACK。 */
bool mod_stepper_get_last_ack_by_addr(mod_stepper_ctx_t *ctx, uint8_t addr, uint8_t *cmd, uint8_t *status);

#endif /* FINAL_GRADUATE_WORK_MOD_STEPPER_H */
