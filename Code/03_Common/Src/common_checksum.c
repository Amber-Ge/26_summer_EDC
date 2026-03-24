/**
 * @file    common_checksum.c
 * @brief   校验算法公共实现。
 * @details
 * 1. 文件作用：实现通用 XOR 校验计算函数。
 * 2. 解耦边界：仅包含纯算法，不依赖硬件和 RTOS。
 * 3. 上层绑定：通信/协议模块可复用该接口生成帧校验字段。
 */
#include "common_checksum.h"

#include <stddef.h>

/**
 * @brief 计算字节缓冲区 XOR 校验值。
 * @param data 待校验数据首地址。
 * @param len 数据长度（字节）。
 * @return uint8_t XOR 校验结果；输入非法时返回 0。
 */
uint8_t common_checksum_xor_u8(const uint8_t *data, uint16_t len)
{
    uint8_t checksum = 0U; // checksum：累计异或结果。

    // 步骤1：参数有效时遍历输入缓冲区逐字节异或。
    if ((data != NULL) && (len > 0U))
    {
        for (uint16_t i = 0U; i < len; i++) // i：字节索引。
        {
            checksum ^= data[i];
        }
    }

    // 步骤2：返回最终校验值。
    return checksum;
}
