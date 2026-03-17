#include "mod_k230.h"
#include "mod_uart_guard.h"
#include "common_checksum.h"
#include <string.h>

/* ========================== [ 1. Static Resources ] ========================== */

static UART_HandleTypeDef *s_p_k230_uart = NULL; // K230 通信串口句柄
static osSemaphoreId_t s_sem_list[MOD_K230_MAX_BIND_SEM]; // 绑定任务使用的接收信号量列表
static osMutexId_t s_p_k230_tx_mutex = NULL; // K230 发送互斥锁

/* RX path: DMA staging buffer + software ring buffer */
static uint8_t s_k230_rx_dma_buf[MOD_K230_RX_DMA_BUF_SIZE]; // DMA 接收暂存缓冲区
static uint8_t s_k230_rx_ring_buf[MOD_K230_RX_RING_BUF_SIZE]; // 软件环形接收缓冲区
static volatile uint16_t s_rx_head = 0U; // 环形缓冲区写指针
static volatile uint16_t s_rx_tail = 0U; // 环形缓冲区读指针

/* TX path: one static buffer to avoid DMA using caller stack memory */
static uint8_t s_k230_tx_buf[MOD_K230_TX_BUF_SIZE]; // 发送缓冲区（避免使用调用者栈内存）

/* k230 private frame protocol (12 bytes, fixed layout). */
#define K230_PROTO_FRAME_SIZE             (12U)
#define K230_PROTO_HEADER_1               (0xAAU)
#define K230_PROTO_HEADER_2               (0xAAU)
#define K230_PROTO_LEN_FIXED              (0x06U)
#define K230_PROTO_CHECKSUM_START_IDX     (2U)
#define K230_PROTO_CHECKSUM_LEN           (7U)  /* [2]..[8] */
#define K230_PROTO_MOTOR1_ID_IDX          (3U)
#define K230_PROTO_ERR1_H_IDX             (4U)
#define K230_PROTO_ERR1_L_IDX             (5U)
#define K230_PROTO_MOTOR2_ID_IDX          (6U)
#define K230_PROTO_ERR2_H_IDX             (7U)
#define K230_PROTO_ERR2_L_IDX             (8U)
#define K230_PROTO_CHECKSUM_IDX           (9U)
#define K230_PROTO_TAIL_1_IDX             (10U)
#define K230_PROTO_TAIL_2_IDX             (11U)
#define K230_PROTO_TAIL_1                 (0x55U)
#define K230_PROTO_TAIL_2                 (0x55U)

/* parser state for streaming bytes from RX ring */
static uint8_t s_k230_parse_buf[K230_PROTO_FRAME_SIZE]; // 协议解析缓存
static uint8_t s_k230_parse_len = 0U; // 当前已缓存的解析字节数


/* ========================== [ 2. Internal Helpers ] ========================== */

/**
 * @brief 进入临界区（关中断）。
 * @return uint32_t 进入前 PRIMASK 状态。
 */
static uint32_t _enter_critical(void)
{
    uint32_t primask = __get_PRIMASK(); // 进入前中断屏蔽状态
    __disable_irq();
    return primask;
}

/**
 * @brief 退出临界区（恢复中断状态）。
 * @param primask 进入临界区前保存的 PRIMASK。
 */
static void _exit_critical(uint32_t primask)
{
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 获取当前接收环形缓冲区已存字节数。
 * @return uint16_t 可读字节数。
 */
static uint16_t _ring_count(void)
{
    uint16_t count; // 缓冲区数据量
    if (s_rx_head >= s_rx_tail)
    {
        count = (uint16_t)(s_rx_head - s_rx_tail);
    }
    else
    {
        count = (uint16_t)(MOD_K230_RX_RING_BUF_SIZE - s_rx_tail + s_rx_head);
    }
    return count;
}

static void _ring_push_byte(uint8_t data)
{
    uint16_t next_head = (uint16_t)(s_rx_head + 1U); // 写入后下一个写指针位置
    if (next_head >= MOD_K230_RX_RING_BUF_SIZE)
    {
        next_head = 0U;
    }

    //1. 缓冲区满时丢弃最旧字节，保留最新数据
    if (next_head == s_rx_tail)
    {
        s_rx_tail = (uint16_t)(s_rx_tail + 1U);
        if (s_rx_tail >= MOD_K230_RX_RING_BUF_SIZE)
        {
            s_rx_tail = 0U;
        }
    }

    //2. 写入新字节并推进写指针
    s_k230_rx_ring_buf[s_rx_head] = data;
    s_rx_head = next_head;
}

static void _ring_push_block(const uint8_t *data, uint16_t len)
{
    //1. 参数有效时逐字节写入环形缓冲区
    if ((data != NULL) && (len > 0U))
    {
        for (uint16_t i = 0U; i < len; i++)
        {
            _ring_push_byte(data[i]);
        }
    }
}

static void _proto_parser_reset(void)
{
    //1. 清空协议解析状态机
    s_k230_parse_len = 0U;
    memset(s_k230_parse_buf, 0, sizeof(s_k230_parse_buf));
}

static bool _proto_frame_is_valid(const uint8_t *frame)
{
    bool result = false; // 帧合法性判断结果

    //1. 校验帧头、长度与帧尾固定字段
    if (frame != NULL)
    {
        if ((frame[0] == K230_PROTO_HEADER_1) &&
            (frame[1] == K230_PROTO_HEADER_2) &&
            (frame[2] == K230_PROTO_LEN_FIXED) &&
            (frame[K230_PROTO_TAIL_1_IDX] == K230_PROTO_TAIL_1) &&
            (frame[K230_PROTO_TAIL_2_IDX] == K230_PROTO_TAIL_2))
        {
            uint8_t checksum = common_checksum_xor_u8(&frame[K230_PROTO_CHECKSUM_START_IDX], K230_PROTO_CHECKSUM_LEN); // 计算 XOR 校验值
            result = (checksum == frame[K230_PROTO_CHECKSUM_IDX]);
        }
    }

    return result;
}

static void _proto_decode_frame(const uint8_t *frame, mod_k230_frame_data_t *out_frame)
{
    //1. 参数有效时按协议字段提取数据
    if ((frame != NULL) && (out_frame != NULL))
    {
        out_frame->motor1_id = frame[K230_PROTO_MOTOR1_ID_IDX];
        out_frame->err1 = (int16_t)(((uint16_t)frame[K230_PROTO_ERR1_H_IDX] << 8U) |
                                    (uint16_t)frame[K230_PROTO_ERR1_L_IDX]);
        out_frame->motor2_id = frame[K230_PROTO_MOTOR2_ID_IDX];
        out_frame->err2 = (int16_t)(((uint16_t)frame[K230_PROTO_ERR2_H_IDX] << 8U) |
                                    (uint16_t)frame[K230_PROTO_ERR2_L_IDX]);
    }
}

static void _proto_resync_after_invalid(void)
{
    uint8_t new_len = 0U; // 重同步后保留的缓存长度

    //1. 在缓存中查找下一组可能的帧头
    for (uint8_t i = 1U; i < K230_PROTO_FRAME_SIZE; i++)
    {
        if (s_k230_parse_buf[i] == K230_PROTO_HEADER_1)
        {
            if (i == (K230_PROTO_FRAME_SIZE - 1U))
            {
                s_k230_parse_buf[0] = K230_PROTO_HEADER_1;
                new_len = 1U;
                break;
            }

            if (s_k230_parse_buf[i + 1U] == K230_PROTO_HEADER_2)
            {
                new_len = (uint8_t)(K230_PROTO_FRAME_SIZE - i);
                memcpy(s_k230_parse_buf, &s_k230_parse_buf[i], new_len);
                break;
            }
        }
    }

    //2. 更新解析缓存长度
    s_k230_parse_len = new_len;
}

static bool _proto_feed_byte(uint8_t byte, mod_k230_frame_data_t *out_frame)
{
    bool got_frame = false; // 是否成功解析出一帧

    //1. 空状态仅接受第一个帧头字节
    if (s_k230_parse_len == 0U)
    {
        if (byte == K230_PROTO_HEADER_1)
        {
            s_k230_parse_buf[0] = byte;
            s_k230_parse_len = 1U;
        }
        return false;
    }

    //2. 第二字节状态仅接受第二个帧头字节
    if (s_k230_parse_len == 1U)
    {
        if (byte == K230_PROTO_HEADER_2)
        {
            s_k230_parse_buf[1] = byte;
            s_k230_parse_len = 2U;
        }
        else if (byte == K230_PROTO_HEADER_1)
        {
            s_k230_parse_buf[0] = K230_PROTO_HEADER_1;
            s_k230_parse_len = 1U;
        }
        else
        {
            s_k230_parse_len = 0U;
        }
        return false;
    }

    //3. 累积后续字节，达到固定帧长后进行校验
    s_k230_parse_buf[s_k230_parse_len] = byte;
    s_k230_parse_len++;

    if (s_k230_parse_len >= K230_PROTO_FRAME_SIZE)
    {
        if (_proto_frame_is_valid(s_k230_parse_buf))
        {
            _proto_decode_frame(s_k230_parse_buf, out_frame);
            got_frame = true;
            s_k230_parse_len = 0U;
        }
        else
        {
            _proto_resync_after_invalid();
        }
    }

    //4. 返回是否解析出完整合法帧
    return got_frame;
}

static bool _tx_lock(void)
{
    //1. 未配置互斥锁时默认视为加锁成功
    if (s_p_k230_tx_mutex == NULL)
    {
        return true;
    }

    return (osMutexAcquire(s_p_k230_tx_mutex, MOD_K230_TX_MUTEX_TIMEOUT_MS) == osOK);
}

static void _tx_unlock(void)
{
    //1. 已配置互斥锁时释放发送锁
    if (s_p_k230_tx_mutex != NULL)
    {
        (void)osMutexRelease(s_p_k230_tx_mutex);
    }
}

static void _notify_all_bound_semaphores(void)
{
    //1. 遍历并释放已绑定信号量，通知上层有新数据可读
    for (uint8_t i = 0U; i < MOD_K230_MAX_BIND_SEM; i++)
    {
        if (s_sem_list[i] != NULL)
        {
            (void)osSemaphoreRelease(s_sem_list[i]);
        }
    }
}

static void _restart_rx_dma(void)
{
    //1. 重启 DMA 接收，若启动失败先停后重试一次
    if (s_p_k230_uart != NULL)
    {
        if (!drv_uart_receive_dma_start(s_p_k230_uart, s_k230_rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE))
        {
            drv_uart_receive_dma_stop(s_p_k230_uart);
            (void)drv_uart_receive_dma_start(s_p_k230_uart, s_k230_rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE);
        }
    }
}

static void _k230_rx_callback_handler(uint16_t len)
{
    //1. 对回调长度做上限保护
    if (len > MOD_K230_RX_DMA_BUF_SIZE)
    {
        len = MOD_K230_RX_DMA_BUF_SIZE;
    }

    //2. 将 DMA 数据搬运到软件环形缓冲并通知上层
    if (len > 0U)
    {
        _ring_push_block(s_k230_rx_dma_buf, len);
        _notify_all_bound_semaphores();
    }

    //3. 重新拉起 DMA 接收
    _restart_rx_dma();
}


/* ========================== [ 3. Public API ] ========================== */

void mod_k230_init(UART_HandleTypeDef *huart)
{
    //1. 参数有效时执行初始化流程
    if (huart != NULL)
    {
        //1.1 若已绑定其他串口，先执行反初始化
        if ((s_p_k230_uart != NULL) && (s_p_k230_uart != huart))
        {
            mod_k230_deinit();
        }

        //1.2 申请 UART 资源所有权，失败直接返回
        if (!mod_uart_guard_claim(huart, MOD_UART_OWNER_K230))
        {
            return;
        }

        s_p_k230_uart = huart;

        //1.3 清空运行时资源与缓存
        memset(s_sem_list, 0, sizeof(s_sem_list));
        memset(s_k230_rx_dma_buf, 0, sizeof(s_k230_rx_dma_buf));
        memset(s_k230_rx_ring_buf, 0, sizeof(s_k230_rx_ring_buf));
        memset(s_k230_tx_buf, 0, sizeof(s_k230_tx_buf));
        s_rx_head = 0U;
        s_rx_tail = 0U;
        _proto_parser_reset();

        //1.4 注册 DMA 回调并启动 DMA 接收，失败则回滚资源
        (void)drv_uart_register_callback(huart, _k230_rx_callback_handler);
        if (!drv_uart_receive_dma_start(huart, s_k230_rx_dma_buf, MOD_K230_RX_DMA_BUF_SIZE))
        {
            (void)drv_uart_register_callback(huart, NULL);
            (void)mod_uart_guard_release(huart, MOD_UART_OWNER_K230);
            s_p_k230_uart = NULL;
        }
    }
}

void mod_k230_deinit(void)
{
    //1. 若串口已绑定，先停止 DMA、注销回调并释放 UART 资源
    if (s_p_k230_uart != NULL)
    {
        drv_uart_receive_dma_stop(s_p_k230_uart);
        (void)drv_uart_register_callback(s_p_k230_uart, NULL);
        (void)mod_uart_guard_release(s_p_k230_uart, MOD_UART_OWNER_K230);
        s_p_k230_uart = NULL;
    }

    //2. 清理模块本地状态
    memset(s_sem_list, 0, sizeof(s_sem_list));
    s_rx_head = 0U;
    s_rx_tail = 0U;
    _proto_parser_reset();
}

bool mod_k230_bind_semaphore(osSemaphoreId_t sem_id)
{
    bool result = false; // 绑定结果

    //1. 参数有效时进入临界区执行绑定
    if (sem_id != NULL)
    {
        uint32_t primask = _enter_critical(); // 临界区状态保存值

        //1.1 已绑定则直接返回成功
        for (uint8_t i = 0U; i < MOD_K230_MAX_BIND_SEM; i++)
        {
            if (s_sem_list[i] == sem_id)
            {
                result = true;
                break;
            }
        }

        //1.2 未绑定则尝试占用空槽位
        if (!result)
        {
            for (uint8_t i = 0U; i < MOD_K230_MAX_BIND_SEM; i++)
            {
                if (s_sem_list[i] == NULL)
                {
                    s_sem_list[i] = sem_id;
                    result = true;
                    break;
                }
            }
        }

        _exit_critical(primask);
    }

    return result;
}

bool mod_k230_unbind_semaphore(osSemaphoreId_t sem_id)
{
    bool result = false; // 解绑结果

    //1. 参数有效时进入临界区执行解绑
    if (sem_id != NULL)
    {
        uint32_t primask = _enter_critical(); // 临界区状态保存值

        for (uint8_t i = 0U; i < MOD_K230_MAX_BIND_SEM; i++)
        {
            if (s_sem_list[i] == sem_id)
            {
                s_sem_list[i] = NULL;
                result = true;
                break;
            }
        }

        _exit_critical(primask);
    }

    return result;
}

void mod_k230_clear_semaphores(void)
{
    uint32_t primask = _enter_critical(); // 临界区状态保存值
    //1. 清空全部信号量绑定
    memset(s_sem_list, 0, sizeof(s_sem_list));
    _exit_critical(primask);
}

bool mod_k230_send_bytes(const uint8_t *data, uint16_t len)
{
    bool result = false; // 发送结果

    //1. 参数与上下文校验
    if ((s_p_k230_uart != NULL) && (data != NULL) && (len > 0U) && (len <= MOD_K230_TX_BUF_SIZE))
    {
        //2. 获取发送锁，避免并发写串口
        if (_tx_lock())
        {
            //3. 串口空闲时拷贝到静态缓冲并 DMA 发送
            if (drv_uart_is_tx_free(s_p_k230_uart))
            {
                memcpy(s_k230_tx_buf, data, len);
                result = drv_uart_send_dma(s_p_k230_uart, s_k230_tx_buf, len);
            }
            _tx_unlock();
        }
    }

    return result;
}

bool mod_k230_is_tx_free(void)
{
    bool result = false; // 串口发送空闲状态

    if (s_p_k230_uart != NULL)
    {
        result = drv_uart_is_tx_free(s_p_k230_uart);
    }

    return result;
}

uint16_t mod_k230_available(void)
{
    uint16_t count; // 当前可读字节数
    uint32_t primask = _enter_critical(); // 临界区状态保存值
    //1. 临界区内读取环形缓冲区长度
    count = _ring_count();
    _exit_critical(primask);
    return count;
}

uint16_t mod_k230_read_bytes(uint8_t *out, uint16_t max_len)
{
    uint16_t read_len = 0U; // 实际读取字节数

    //1. 参数有效时从环形缓冲区读取数据
    if ((out != NULL) && (max_len > 0U))
    {
        uint32_t primask = _enter_critical(); // 临界区状态保存值

        //1.1 按请求长度与可用数据量逐字节弹出
        while ((read_len < max_len) && (s_rx_tail != s_rx_head))
        {
            out[read_len] = s_k230_rx_ring_buf[s_rx_tail];
            read_len++;

            s_rx_tail = (uint16_t)(s_rx_tail + 1U);
            if (s_rx_tail >= MOD_K230_RX_RING_BUF_SIZE)
            {
                s_rx_tail = 0U;
            }
        }

        _exit_critical(primask);
    }

    return read_len;
}

void mod_k230_clear_rx_buffer(void)
{
    uint32_t primask = _enter_critical(); // 临界区状态保存值
    //1. 清空环形缓冲区读写指针
    s_rx_head = 0U;
    s_rx_tail = 0U;
    _exit_critical(primask);
    //2. 清空协议解析状态机
    _proto_parser_reset();
}

void mod_k230_set_tx_mutex(osMutexId_t mutex_id)
{
    uint32_t primask = _enter_critical(); // 临界区状态保存值
    //1. 原子更新发送互斥锁句柄
    s_p_k230_tx_mutex = mutex_id;
    _exit_critical(primask);
}

bool mod_k230_get_latest_frame(mod_k230_frame_data_t *out_frame)
{
    bool got_latest = false; // 是否至少解析出一帧
    uint8_t read_buf[64]; // 分批读取缓存
    uint16_t read_len; // 本批读取长度
    mod_k230_frame_data_t latest_frame = {0}; // 最新一帧解析结果

    //1. 输出指针校验
    if (out_frame == NULL)
    {
        return false;
    }

    //2. 读取所有可用字节并逐字节喂给协议解析器
    do
    {
        read_len = mod_k230_read_bytes(read_buf, (uint16_t)sizeof(read_buf));

        for (uint16_t i = 0U; i < read_len; i++)
        {
            mod_k230_frame_data_t parsed_frame;
            //2.1 每解析成功一帧就覆盖 latest_frame，最终保留“最新帧”
            if (_proto_feed_byte(read_buf[i], &parsed_frame))
            {
                latest_frame = parsed_frame;
                got_latest = true;
            }
        }
    } while (read_len > 0U);

    //3. 若解析到有效帧则输出结果
    if (got_latest)
    {
        *out_frame = latest_frame;
    }

    return got_latest;
}
