/**
 * @file    common_checksum.c
 * @brief   校验算法公共实现。
 * @details
 * 1. 文件作用：实现通用校验和计算函数。
 * 2. 上下层绑定：上层由通信/协议模块调用；下层不依赖硬件接口。
 */
#include "common_checksum.h"
#include <stddef.h>

/**
 * @brief 计算字节缓冲区的 XOR 校验值。
 * @param data 待校验数据首地址。
 * @param len 数据长度（字节）。
 * @return XOR 校验结果。
 */
uint8_t common_checksum_xor_u8(const uint8_t *data, uint16_t len)
{
    uint8_t checksum = 0U; // 累计 XOR 校验值

    //1. 参数有效时遍历输入缓冲区并逐字节异或
    if ((data != NULL) && (len > 0U))
    {
        for (uint16_t i = 0U; i < len; i++) // i：字节索引
        {
            checksum ^= data[i];
        }
    }

    //2. 返回最终校验结果
    return checksum;
}

