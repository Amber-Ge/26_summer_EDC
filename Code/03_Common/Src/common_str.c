/**
 * @file    common_str.c
 * @brief   字符串公共工具实现。
 * @details
 * 1. 文件作用：实现整数/浮点到字符串的轻量转换工具。
 * 2. 解耦边界：仅做格式化，不依赖外设驱动和 RTOS。
 * 3. 上层绑定：显示、调试、协议打包等模块统一复用。
 */
#include "common_str.h"

/**
 * @brief 无符号整数转字符串内部工具函数。
 * @details
 * 该函数将 `uint32_t` 转为十进制字符串，并返回实际写入长度。
 *
 * @param val 待转换无符号整数。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区总长度（字节）。
 * @param p_len 输出字符串长度（不含终止符）。
 * @return true 转换成功。
 * @return false 参数非法或缓冲区不足。
 */
static bool _u32_to_str_tool(uint32_t val, char *p_out, uint16_t size, uint16_t *p_len)
{
    bool result = false; // result：转换是否成功。
    char tmp[10]; // tmp：逆序暂存数字字符（uint32 最大 10 位）。
    uint8_t i = 0U; // i：逆序写入索引。
    uint8_t j = 0U; // j：正序拷贝索引。

    // 步骤1：校验输出缓冲区、长度指针与容量。
    if ((p_out != NULL) && (p_len != NULL) && (size > 0U))
    {
        // 步骤2：特判 0，避免后续循环不执行导致空串。
        if (val == 0U)
        {
            if (size >= 2U)
            {
                p_out[0] = '0';
                p_out[1] = '\0';
                *p_len = 1U;
                result = true;
            }
        }
        else
        {
            // 步骤3：按低位到高位提取数字，逆序写入临时数组。
            while ((val > 0U) && (i < (uint8_t)sizeof(tmp)))
            {
                tmp[i] = (char)((val % 10U) + '0');
                val /= 10U;
                i++;
            }

            // 步骤4：容量充足时，将逆序字符翻转写回输出缓冲区。
            if ((uint16_t)(i + 1U) <= size)
            {
                for (j = 0U; j < i; j++)
                {
                    p_out[j] = tmp[i - 1U - j];
                }
                p_out[i] = '\0';
                *p_len = (uint16_t)i;
                result = true;
            }
        }
    }

    // 步骤5：返回转换结果。
    return result;
}

/**
 * @brief 无符号整数转十进制字符串。
 * @param val 待转换无符号整数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return true 转换成功。
 * @return false 转换失败。
 */
bool common_uint_to_str(uint32_t val, char *p_out, uint16_t size)
{
    bool result = false; // result：接口执行结果。
    uint16_t len = 0U; // len：实际写入长度。

    // 步骤1：清空输出起始位并调用内部工具转换。
    if (p_out != NULL)
    {
        p_out[0] = '\0';
        result = _u32_to_str_tool(val, p_out, size, &len);
    }

    // 步骤2：返回结果。
    return result;
}

/**
 * @brief 有符号整数转十进制字符串。
 * @param val 待转换有符号整数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return true 转换成功。
 * @return false 转换失败。
 */
bool common_int_to_str(int32_t val, char *p_out, uint16_t size)
{
    bool result = false; // result：接口执行结果。
    uint16_t len = 0U; // len：数字部分长度。
    uint32_t abs_val = 0U; // abs_val：负数绝对值（无符号表示）。

    // 步骤1：参数校验（最小需容纳 "-0\0"）。
    if ((p_out != NULL) && (size >= 2U))
    {
        p_out[0] = '\0';

        // 步骤2：非负数直接转换。
        if (val >= 0)
        {
            result = _u32_to_str_tool((uint32_t)val, p_out, size, &len);
        }
        else
        {
            // 步骤3：负数先写符号，再转换绝对值。
            p_out[0] = '-';
            abs_val = (uint32_t)(-(int64_t)val);
            result = _u32_to_str_tool(abs_val, &p_out[1], (uint16_t)(size - 1U), &len);
        }
    }

    // 步骤4：返回结果。
    return result;
}

/**
 * @brief 浮点数按固定精度转字符串。
 * @param val 待转换浮点数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return true 转换成功。
 * @return false 转换失败。
 */
bool common_float_to_str(float val, char *p_out, uint16_t size)
{
    bool result = false; // result：接口执行结果。
    uint16_t idx = 0U; // idx：当前写入位置。
    uint16_t len = 0U; // len：整数部分写入长度。
    uint32_t int_part = 0U; // int_part：整数部分。
    uint32_t frac_part = 0U; // frac_part：小数部分（按精度缩放后）。
    float f_abs = 0.0f; // f_abs：输入绝对值。
    uint32_t scale = 1U; // scale：小数缩放因子（10^precision）。
    uint8_t i = 0U; // i：小数位循环索引。

    // 步骤1：参数校验（预留符号、整数、小数点和小数位空间）。
    if ((p_out != NULL) && (size >= 10U))
    {
        p_out[0] = '\0';
        f_abs = (val < 0.0f) ? (-val) : val;

        // 步骤2：处理负号。
        if (val < 0.0f)
        {
            p_out[idx++] = '-';
        }

        // 步骤3：计算缩放因子 10^COMMON_STR_FLOAT_PRECISION。
        for (i = 0U; i < COMMON_STR_FLOAT_PRECISION; i++)
        {
            scale *= 10U;
        }

        // 步骤4：分离整数和小数，并对小数执行四舍五入。
        int_part = (uint32_t)f_abs;
        frac_part = (uint32_t)((f_abs - (float)int_part) * (float)scale + 0.5f);

        // 步骤5：处理四舍五入导致的小数进位。
        if (frac_part >= scale)
        {
            int_part += 1U;
            frac_part = 0U;
        }

        // 步骤6：写入整数部分。
        if (_u32_to_str_tool(int_part, &p_out[idx], (uint16_t)(size - idx), &len))
        {
            idx += len;

            // 步骤7：空间充足时写入小数点与固定精度小数位。
            if ((uint16_t)(idx + COMMON_STR_FLOAT_PRECISION + 2U) <= size)
            {
                p_out[idx++] = '.';

                for (i = 0U; i < COMMON_STR_FLOAT_PRECISION; i++)
                {
                    uint32_t sub_scale = 1U; // sub_scale：当前位对应的除数。

                    for (uint8_t k = 0U; k < (COMMON_STR_FLOAT_PRECISION - 1U - i); k++)
                    {
                        sub_scale *= 10U;
                    }

                    p_out[idx++] = (char)(((frac_part / sub_scale) % 10U) + '0');
                }

                p_out[idx] = '\0';
                result = true;
            }
        }
    }

    // 步骤8：返回结果。
    return result;
}
