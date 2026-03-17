#ifndef FINAL_GRADUATE_WORK_MOD_VOFA_H
#define FINAL_GRADUATE_WORK_MOD_VOFA_H

#include "drv_uart.h"
#include "common_str.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>
#include "mod_uart_guard.h"
#include <string.h>



#define MOD_VOFA_TX_BUF_SIZE            (256U)
#define MOD_VOFA_RX_BUF_SIZE            (64U)
#define MOD_VOFA_MAX_BIND_SEM           (4U)
#define MOD_VOFA_TX_MUTEX_TIMEOUT_MS    (5U)

typedef enum
{
    VOFA_CMD_NONE = 0, // 无命令
    VOFA_CMD_START, // 启动命令
    VOFA_CMD_STOP, // 停止命令
    VOFA_CMD_MAX // 命令类型数量上限
} vofa_cmd_id_t;

typedef struct
{
    UART_HandleTypeDef *huart; // 绑定的串口句柄
    osSemaphoreId_t sem_list[MOD_VOFA_MAX_BIND_SEM]; // 绑定的事件信号量列表
    uint8_t sem_count; // 有效信号量数量
    osMutexId_t tx_mutex; // 发送互斥锁句柄
} mod_vofa_bind_t;

typedef struct
{
    bool inited; // 模块是否已初始化
    bool bound; // 模块是否已完成绑定
    mod_vofa_bind_t bind; // 绑定参数集合
    vofa_cmd_id_t last_cmd; // 最近一次解析到的命令
    char tx_buf[MOD_VOFA_TX_BUF_SIZE]; // 文本发送缓冲区
    uint8_t rx_buf[MOD_VOFA_RX_BUF_SIZE]; // DMA接收缓冲区
} mod_vofa_ctx_t;

/* ========================== [ Ctx-based API ] ========================== */

mod_vofa_ctx_t *mod_vofa_get_default_ctx(void);
bool mod_vofa_ctx_init(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind);
bool mod_vofa_bind(mod_vofa_ctx_t *ctx, const mod_vofa_bind_t *bind);
void mod_vofa_unbind(mod_vofa_ctx_t *ctx);
bool mod_vofa_is_bound(const mod_vofa_ctx_t *ctx);

bool mod_vofa_add_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id);
bool mod_vofa_remove_semaphore(mod_vofa_ctx_t *ctx, osSemaphoreId_t sem_id);
void mod_vofa_clear_semaphores(mod_vofa_ctx_t *ctx);
void mod_vofa_set_tx_mutex(mod_vofa_ctx_t *ctx, osMutexId_t mutex_id);

vofa_cmd_id_t mod_vofa_get_command_ctx(mod_vofa_ctx_t *ctx);
bool mod_vofa_send_float_ctx(mod_vofa_ctx_t *ctx, const char *tag, const float *arr, uint16_t n);
bool mod_vofa_send_int_ctx(mod_vofa_ctx_t *ctx, const char *tag, const int32_t *arr, uint16_t n);
bool mod_vofa_send_uint_ctx(mod_vofa_ctx_t *ctx, const char *tag, const uint32_t *arr, uint16_t n);
bool mod_vofa_send_string_ctx(mod_vofa_ctx_t *ctx, const char *str);

/* ========================== [ Legacy Compatible API ] ========================== */

void mod_vofa_init(UART_HandleTypeDef *huart, osSemaphoreId_t sem_id);
vofa_cmd_id_t mod_vofa_get_command(void);
bool mod_vofa_send_float(const char *tag, const float *arr, uint16_t n);
bool mod_vofa_send_int(const char *tag, const int32_t *arr, uint16_t n);
bool mod_vofa_send_uint(const char *tag, const uint32_t *arr, uint16_t n);
bool mod_vofa_send_string(const char *str);

#endif
