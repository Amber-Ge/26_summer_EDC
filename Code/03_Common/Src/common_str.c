/**
 * @file    common_str.c
 * @brief   字符串公共工具实现。
 * @details
 * 1. 文件作用：实现字符串解析、格式化与辅助工具函数。
 * 2. 上下层绑定：上层由模块与任务层复用；下层不依赖硬件接口。
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
    bool result = false; // 转换结果标志
    char tmp[10]; // 逆序暂存数字字符（uint32_t 最大 10 位）
    uint8_t i = 0U; // 逆序写入索引
    uint8_t j = 0U; // 正序拷贝索引

    //1. 参数校验：输出缓冲区、长度指针与容量必须有效
    if ((p_out != NULL) && (p_len != NULL) && (size > 0U))
    {
        //2. 特判 0，避免后续循环不执行导致空串
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
            //3. 先按低位到高位提取数字，逆序存放到临时数组
            while ((val > 0U) && (i < (uint8_t)sizeof(tmp)))
            {
                tmp[i] = (char)((val % 10U) + '0');
                val /= 10U;
                i++;
            }

            //4. 检查输出缓冲区容量，再将逆序字符翻转写回
            if ((uint16_t)(i + 1U) <= size)
            {
                for (j = 0U; j < i; j++) // 循环计数器
                {
                    p_out[j] = tmp[i - 1U - j];
                }
                p_out[i] = '\0';
                *p_len = (uint16_t)i;
                result = true;
            }
        }
    }

    //5. 返回转换结果
    return result;
}

/**
 * @brief 无符号整数转十进制字符串。
 * @param val 待转换无符号整数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return 转换成功返回 `true`，失败返回 `false`。
 */
bool common_uint_to_str(uint32_t val, char *p_out, uint16_t size)
{
    bool result = false; // 接口执行结果
    uint16_t len = 0U; // 实际字符串长度

    //1. 先将输出缓冲区清空，再调用内部转换工具
    if (p_out != NULL)
    {
        p_out[0] = '\0';
        result = _u32_to_str_tool(val, p_out, size, &len);
    }

    //2. 返回转换结果
    return result;
}

/**
 * @brief 有符号整数转十进制字符串。
 * @param val 待转换有符号整数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return 转换成功返回 `true`，失败返回 `false`。
 */
bool common_int_to_str(int32_t val, char *p_out, uint16_t size)
{
    bool result = false; // 接口执行结果
    uint16_t len = 0U; // 数字部分长度
    uint32_t abs_val = 0U; // 负数绝对值（无符号表示）

    //1. 参数校验：最小需容纳“-0\0”长度
    if ((p_out != NULL) && (size >= 2U))
    {
        p_out[0] = '\0';

        //2. 非负数直接转无符号字符串
        if (val >= 0)
        {
            result = _u32_to_str_tool((uint32_t)val, p_out, size, &len);
        }
        else
        {
            //3. 负数先写符号，再转换绝对值
            p_out[0] = '-';
            abs_val = (uint32_t)(-(int64_t)val);
            result = _u32_to_str_tool(abs_val, &p_out[1], (uint16_t)(size - 1U), &len);
        }
    }

    //4. 返回转换结果
    return result;
}

/**
 * @brief 浮点数按固定精度转字符串。
 * @param val 待转换浮点数。
 * @param p_out 输出缓冲区。
 * @param size 输出缓冲区容量。
 * @return 转换成功返回 `true`，失败返回 `false`。
 */
bool common_float_to_str(float val, char *p_out, uint16_t size)
{
    bool result = false; // 接口执行结果
    uint16_t idx = 0U; // 当前输出写入位置
    uint16_t len = 0U; // 整数部分写入长度
    uint32_t int_part = 0U; // 整数部分
    uint32_t frac_part = 0U; // 小数部分（已按精度缩放）
    float f_abs = 0.0f; // 输入绝对值
    uint32_t scale = 1U; // 小数缩放因子（10^precision）
    uint8_t i = 0U; // 小数位循环索引

    //1. 参数校验：留出足够空间容纳符号、整数、小数点与小数位
    if ((p_out != NULL) && (size >= 10U))
    {
        p_out[0] = '\0';
        f_abs = (val < 0.0f) ? (-val) : val;

        //2. 处理负号
        if (val < 0.0f)
        {
            p_out[idx++] = '-';
        }

        //3. 计算小数缩放因子
        for (i = 0U; i < COMMON_STR_FLOAT_PRECISION; i++) // 循环计数器
        {
            scale *= 10U;
        }

        //4. 分离整数和小数，并对小数做四舍五入
        int_part = (uint32_t)f_abs;
        frac_part = (uint32_t)((f_abs - (float)int_part) * (float)scale + 0.5f);

        //5. 处理四舍五入导致的小数进位
        if (frac_part >= scale)
        {
            int_part += 1U;
            frac_part = 0U;
        }

        //6. 写入整数部分
        if (_u32_to_str_tool(int_part, &p_out[idx], (uint16_t)(size - idx), &len))
        {
            idx += len;
            if ((uint16_t)(idx + COMMON_STR_FLOAT_PRECISION + 2U) <= size)
            {
                //7. 写入小数点和固定精度小数位
                p_out[idx++] = '.';
                for (i = 0U; i < COMMON_STR_FLOAT_PRECISION; i++) // 循环计数器
                {
                    uint32_t sub_scale = 1U; // 当前小数位对应除数
                    for (uint8_t k = 0U; k < (COMMON_STR_FLOAT_PRECISION - 1U - i); k++) // 循环计数器
                    {
                        sub_scale *= 10U;
                    }
                    p_out[idx++] = (char)((frac_part / sub_scale) % 10U + '0');
                }
                p_out[idx] = '\0';
                result = true;
            }
        }
    }

    //8. 返回转换结果
    return result;
}

