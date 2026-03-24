/**
 * @file    mod_uart_guard.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   UART 资源仲裁实现。
 * @details
 * 1. 文件作用：实现 UART 归属申请、释放、查询，以及 owner+claimant 的引用计数管理。
 * 2. 解耦边界：只负责资源仲裁，不参与具体收发和协议逻辑。
 * 3. 并发策略：通过关中断临界区保证资源表读写一致性。
 */

#include "mod_uart_guard.h"

static mod_uart_owner_e s_uart_owner[DRV_UART_PORT_COUNT] =
{
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE,
    MOD_UART_OWNER_NONE
};

static uint8_t s_uart_claim_depth[DRV_UART_PORT_COUNT] =
{
    0U, 0U, 0U, 0U, 0U, 0U
};

static const void *s_uart_claimant[DRV_UART_PORT_COUNT] =
{
    NULL, NULL, NULL, NULL, NULL, NULL
};

/**
 * @brief 进入临界区（关中断）。
 */
static uint32_t _critical_enter(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/**
 * @brief 退出临界区（恢复中断状态）。
 */
static void _critical_exit(uint32_t primask)
{
    __set_PRIMASK(primask);
}

/**
 * @brief 解析 UART 端口索引。
 */
static int8_t _resolve_uart_index(UART_HandleTypeDef *huart)
{
    if ((huart == NULL) || (huart->Instance == NULL))
    {
        return -1;
    }

    return drv_uart_get_port_index(huart->Instance);
}

bool mod_uart_guard_claim(UART_HandleTypeDef *huart, mod_uart_owner_e owner)
{
    const void *compat_claimant;

    if (owner == MOD_UART_OWNER_NONE)
    {
        return false;
    }

    /* 兼容模式：旧接口按 owner 维度重入，claimant 使用 owner 哨兵值。 */
    compat_claimant = (const void *)(uintptr_t)owner;
    return mod_uart_guard_claim_ctx(huart, owner, compat_claimant);
}

bool mod_uart_guard_release(UART_HandleTypeDef *huart, mod_uart_owner_e owner)
{
    const void *compat_claimant;

    if (owner == MOD_UART_OWNER_NONE)
    {
        return false;
    }

    compat_claimant = (const void *)(uintptr_t)owner;
    return mod_uart_guard_release_ctx(huart, owner, compat_claimant);
}

bool mod_uart_guard_claim_ctx(UART_HandleTypeDef *huart, mod_uart_owner_e owner, const void *claimant)
{
    int8_t idx;
    uint32_t primask;
    bool result = false;

    if ((owner == MOD_UART_OWNER_NONE) || (claimant == NULL))
    {
        return false;
    }

    idx = _resolve_uart_index(huart);
    if (idx < 0)
    {
        return false;
    }

    primask = _critical_enter();

    if (s_uart_owner[(uint8_t)idx] == MOD_UART_OWNER_NONE)
    {
        s_uart_owner[(uint8_t)idx] = owner;
        s_uart_claimant[(uint8_t)idx] = claimant;
        s_uart_claim_depth[(uint8_t)idx] = 1U;
        result = true;
    }
    else if ((s_uart_owner[(uint8_t)idx] == owner) && (s_uart_claimant[(uint8_t)idx] == claimant))
    {
        if (s_uart_claim_depth[(uint8_t)idx] < 255U)
        {
            s_uart_claim_depth[(uint8_t)idx]++;
            result = true;
        }
    }

    _critical_exit(primask);
    return result;
}

bool mod_uart_guard_release_ctx(UART_HandleTypeDef *huart, mod_uart_owner_e owner, const void *claimant)
{
    int8_t idx;
    uint32_t primask;
    bool result = false;

    if ((owner == MOD_UART_OWNER_NONE) || (claimant == NULL))
    {
        return false;
    }

    idx = _resolve_uart_index(huart);
    if (idx < 0)
    {
        return false;
    }

    primask = _critical_enter();

    if ((s_uart_owner[(uint8_t)idx] == owner) &&
        (s_uart_claimant[(uint8_t)idx] == claimant) &&
        (s_uart_claim_depth[(uint8_t)idx] > 0U))
    {
        s_uart_claim_depth[(uint8_t)idx]--;
        if (s_uart_claim_depth[(uint8_t)idx] == 0U)
        {
            s_uart_owner[(uint8_t)idx] = MOD_UART_OWNER_NONE;
            s_uart_claimant[(uint8_t)idx] = NULL;
        }
        result = true;
    }

    _critical_exit(primask);
    return result;
}

mod_uart_owner_e mod_uart_guard_get_owner(UART_HandleTypeDef *huart)
{
    int8_t idx;
    uint32_t primask;
    mod_uart_owner_e owner = MOD_UART_OWNER_NONE;

    idx = _resolve_uart_index(huart);
    if (idx < 0)
    {
        return MOD_UART_OWNER_NONE;
    }

    primask = _critical_enter();
    owner = s_uart_owner[(uint8_t)idx];
    _critical_exit(primask);

    return owner;
}

uint8_t mod_uart_guard_get_claim_depth(UART_HandleTypeDef *huart)
{
    int8_t idx;
    uint32_t primask;
    uint8_t depth = 0U;

    idx = _resolve_uart_index(huart);
    if (idx < 0)
    {
        return 0U;
    }

    primask = _critical_enter();
    depth = s_uart_claim_depth[(uint8_t)idx];
    _critical_exit(primask);

    return depth;
}

const void *mod_uart_guard_get_claimant(UART_HandleTypeDef *huart)
{
    int8_t idx;
    uint32_t primask;
    const void *claimant = NULL;

    idx = _resolve_uart_index(huart);
    if (idx < 0)
    {
        return NULL;
    }

    primask = _critical_enter();
    claimant = s_uart_claimant[(uint8_t)idx];
    _critical_exit(primask);

    return claimant;
}

