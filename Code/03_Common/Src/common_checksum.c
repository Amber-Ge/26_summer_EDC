#include "common_checksum.h"
#include <stddef.h>

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
