/**
 ******************************************************************************
 * @file    mod_k230.h
 * @brief   K230 串口协议模块接口定义
 * @details
 * 提供 K230 通信的 UART 绑定、DMA 收发、环形缓冲区读取和协议帧解析能力。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_K230_H
#define FINAL_GRADUATE_WORK_MOD_K230_H // 头文件防重复包含宏

#include "drv_uart.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>

#define MOD_K230_RX_DMA_BUF_SIZE      (256U) // DMA接收缓冲区大小，单位字节
#define MOD_K230_RX_RING_BUF_SIZE     (1024U) // 环形接收缓冲区大小，单位字节
#define MOD_K230_TX_BUF_SIZE          (512U) // 发送缓冲区大小，单位字节
#define MOD_K230_MAX_BIND_SEM         (4U) // 最大绑定信号量个数
#define MOD_K230_TX_MUTEX_TIMEOUT_MS  (5U) // 发送互斥锁超时，单位 ms

/**
 * @brief K230 协议帧解析结果结构体。
 */
typedef struct
{
    uint8_t motor1_id; // 帧中电机1标识
    int16_t err1; // 电机1误差值
    uint8_t motor2_id; // 帧中电机2标识
    int16_t err2; // 电机2误差值
} mod_k230_frame_data_t;

/**
 * @brief 初始化 K230 模块并绑定 UART。
 * @param huart UART 句柄指针。
 */
void mod_k230_init(UART_HandleTypeDef *huart);

/**
 * @brief 反初始化 K230 模块并释放资源。
 */
void mod_k230_deinit(void);

/**
 * @brief 绑定一个事件信号量。
 * @details
 * 当模块收到数据并完成处理后，会释放已绑定的信号量。
 *
 * @param sem_id 待绑定信号量句柄。
 * @return true 绑定成功。
 * @return false 绑定失败（重复、容量满或参数无效）。
 */
bool mod_k230_bind_semaphore(osSemaphoreId_t sem_id);

/**
 * @brief 解绑一个事件信号量。
 * @param sem_id 待解绑信号量句柄。
 * @return true 解绑成功。
 * @return false 解绑失败（未找到或参数无效）。
 */
bool mod_k230_unbind_semaphore(osSemaphoreId_t sem_id);

/**
 * @brief 清空全部已绑定信号量。
 */
void mod_k230_clear_semaphores(void);

/**
 * @brief 发送原始字节数据。
 * @details
 * 数据会先拷贝到模块内部缓冲区，再通过 DMA 启动发送。
 *
 * @param data 待发送数据首地址。
 * @param len 待发送长度（字节）。
 * @return true 发送启动成功。
 * @return false 参数无效或发送启动失败。
 */
bool mod_k230_send_bytes(const uint8_t *data, uint16_t len);

/**
 * @brief 查询发送通道是否空闲。
 * @return true 可发送。
 * @return false 通道忙或模块未就绪。
 */
bool mod_k230_is_tx_free(void);

/**
 * @brief 设置发送互斥锁句柄。
 * @param mutex_id 互斥锁句柄，传 `NULL` 表示不使用互斥锁。
 */
void mod_k230_set_tx_mutex(osMutexId_t mutex_id);

/**
 * @brief 获取当前接收缓存可读字节数。
 * @return uint16_t 可读字节数。
 */
uint16_t mod_k230_available(void);

/**
 * @brief 从接收环形缓冲区读取数据。
 * @param out 输出缓冲区地址。
 * @param max_len 期望读取的最大长度。
 * @return uint16_t 实际读取字节数。
 */
uint16_t mod_k230_read_bytes(uint8_t *out, uint16_t max_len);

/**
 * @brief 清空接收缓存。
 */
void mod_k230_clear_rx_buffer(void);

/**
 * @brief 从接收流解析最新有效协议帧。
 * @param out_frame 输出参数，用于返回解析后的帧数据。
 * @return true 成功解析到有效帧。
 * @return false 当前无有效帧或参数无效。
 */
bool mod_k230_get_latest_frame(mod_k230_frame_data_t *out_frame);

#endif /* FINAL_GRADUATE_WORK_MOD_K230_H */
