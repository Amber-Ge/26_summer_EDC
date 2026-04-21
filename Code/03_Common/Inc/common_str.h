/**
* @file    common_str.h
 * @brief   通用数值转字符串接口定义。
 * @details
 * 1. 文件作用：提供整数与浮点数到 C 字符串的安全转换接口。
 * 2. 解耦边界：仅包含格式化算法，不依赖外设驱动和 RTOS。
 * 3. 上层调用：显示、调试、协议打包模块可统一复用。
 * 4. 生命周期：无状态工具函数，调用方负责输出缓冲区的分配与生命周期。
 */
#ifndef FINAL_GRADUATE_WORK_COMMON_STR_H
#define FINAL_GRADUATE_WORK_COMMON_STR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** 浮点字符串转换默认小数位数。 */
#define COMMON_STR_FLOAT_PRECISION (3U)

/**
 * @brief 将无符号 32 位整数转换为十进制字符串。
 * @param val 待转换数值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区长度（字节）。
 * @return true 转换成功。
 * @return false 参数非法或缓冲区不足。
 */
bool common_uint_to_str(uint32_t val, char *p_out, uint16_t size);

/**
 * @brief 将有符号 32 位整数转换为十进制字符串。
 * @param val 待转换数值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区长度（字节）。
 * @return true 转换成功。
 * @return false 参数非法或缓冲区不足。
 */
bool common_int_to_str(int32_t val, char *p_out, uint16_t size);

/**
 * @brief 将浮点数按固定精度转换为字符串。
 * @details
 * 小数位数由 `COMMON_STR_FLOAT_PRECISION` 控制。
 *
 * @param val 待转换浮点值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区长度（字节）。
 * @return true 转换成功。
 * @return false 参数非法或缓冲区不足。
 */
bool common_float_to_str(float val, char *p_out, uint16_t size);

#endif /* FINAL_GRADUATE_WORK_COMMON_STR_H */
