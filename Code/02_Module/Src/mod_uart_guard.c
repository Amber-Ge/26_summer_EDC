#include "mod_uart_guard.h"
#include <stdint.h>

#define MOD_UART_GUARD_UART_COUNT (6U) // 支持仲裁的 UART 实例数量

// UART 拥有者状态表，索引与 UART 实例固定映射
static mod_uart_owner_e s_uart_owner[MOD_UART_GUARD_UART_COUNT] =
{
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE
};

/**
 * @brief 将 UART 外设实例映射为资源表索引。
 * @param instance UART 硬件实例指针。
 * @return int8_t 成功返回 [0,5]，失败返回 -1。
 */
static int8_t _get_uart_index(USART_TypeDef *instance)
{
    int8_t idx = -1; // 映射结果索引

    //1. 根据 UART 实例地址执行固定映射
    switch ((uint32_t)instance)
    {
    case (uint32_t)USART1: idx = 0; break;
    case (uint32_t)USART2: idx = 1; break;
    case (uint32_t)USART3: idx = 2; break;
    case (uint32_t)UART4:  idx = 3; break;
    case (uint32_t)UART5:  idx = 4; break;
    case (uint32_t)USART6: idx = 5; break;
    default:               idx = -1; break;
    }

    return idx;
}

/**
 * @brief 进入临界区（关中断）。
 * @return uint32_t 进入前 PRIMASK 状态。
 */
static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK(); // 保存进入前中断屏蔽状态
    __disable_irq();
    return primask;
}

/**
 * @brief 退出临界区（恢复中断状态）。
 * @param primask 进入临界区前保存的 PRIMASK。
 */
static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

bool mod_uart_guard_claim(UART_HandleTypeDef *huart, mod_uart_owner_e owner)
{
    bool result = false; // 申请结果
    int8_t idx; // UART 映射索引
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验：句柄不能为空，拥有者不能为 NONE
    if ((huart == NULL) || (owner == MOD_UART_OWNER_NONE))
    {
        return false;
    }

    //2. 解析 UART 对应索引
    idx = _get_uart_index(huart->Instance);
    if (idx < 0)
    {
        return false;
    }

    //3. 进入临界区，避免并发访问资源表
    primask = _critical_enter();

    //4. 若当前无拥有者或已被同一拥有者占用，则申请成功
    if ((s_uart_owner[(uint8_t)idx] == MOD_UART_OWNER_NONE) ||
        (s_uart_owner[(uint8_t)idx] == owner))
    {
        s_uart_owner[(uint8_t)idx] = owner;
        result = true;
    }

    _critical_exit(primask);
    return result;
}

bool mod_uart_guard_release(UART_HandleTypeDef *huart, mod_uart_owner_e owner)
{
    bool result = false; // 释放结果
    int8_t idx; // UART 映射索引
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验：句柄不能为空，拥有者不能为 NONE
    if ((huart == NULL) || (owner == MOD_UART_OWNER_NONE))
    {
        return false;
    }

    //2. 解析 UART 对应索引
    idx = _get_uart_index(huart->Instance);
    if (idx < 0)
    {
        return false;
    }

    //3. 进入临界区，仅允许当前拥有者释放
    primask = _critical_enter();

    if (s_uart_owner[(uint8_t)idx] == owner)
    {
        s_uart_owner[(uint8_t)idx] = MOD_UART_OWNER_NONE;
        result = true;
    }

    _critical_exit(primask);
    return result;
}

mod_uart_owner_e mod_uart_guard_get_owner(UART_HandleTypeDef *huart)
{
    int8_t idx; // UART 映射索引
    mod_uart_owner_e owner = MOD_UART_OWNER_NONE; // 当前拥有者返回值
    uint32_t primask; // 临界区状态保存值

    //1. 参数校验
    if (huart == NULL)
    {
        return MOD_UART_OWNER_NONE;
    }

    //2. 解析 UART 对应索引
    idx = _get_uart_index(huart->Instance);
    if (idx < 0)
    {
        return MOD_UART_OWNER_NONE;
    }

    //3. 在临界区读取拥有者状态，保证读取一致性
    primask = _critical_enter();
    owner = s_uart_owner[(uint8_t)idx];
    _critical_exit(primask);
    return owner;
}
