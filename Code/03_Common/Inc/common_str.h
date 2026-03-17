/**
 ******************************************************************************
 * @file    common_str.h
 * @brief   通用数值转字符串接口定义
 * @details
 * 提供无符号整数、有符号整数和浮点数到 C 字符串的安全转换接口。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_COMMON_STR_H
#define FINAL_GRADUATE_WORK_COMMON_STR_H // 头文件防重复包含宏

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/** 浮点字符串转换的小数位数 */
#define COMMON_STR_FLOAT_PRECISION    (3U) // 浮点转字符串默认小数位数

/**
 * @brief 将无符号 32 位整数转换为字符串。
 * @param val 待转换数值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区总大小（字节）。
 * @return true 转换成功。
 * @return false 参数无效或缓冲区不足。
 */
bool common_uint_to_str(uint32_t val, char *p_out, uint16_t size);

/**
 * @brief 将有符号 32 位整数转换为字符串。
 * @param val 待转换数值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区总大小（字节）。
 * @return true 转换成功。
 * @return false 参数无效或缓冲区不足。
 */
bool common_int_to_str(int32_t val, char *p_out, uint16_t size);

/**
 * @brief 将浮点数转换为字符串。
 * @details
 * 转换精度由 `COMMON_STR_FLOAT_PRECISION` 控制。
 *
 * @param val 待转换浮点值。
 * @param p_out 输出缓冲区首地址。
 * @param size 输出缓冲区总大小（字节）。
 * @return true 转换成功。
 * @return false 参数无效或缓冲区不足。
 */
bool common_float_to_str(float val, char *p_out, uint16_t size);

#endif /* FINAL_GRADUATE_WORK_COMMON_STR_H */
