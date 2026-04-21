/**
* @file    common_checksum.h
 * @brief   通用校验算法接口定义。
 * @details
 * 1. 文件作用：声明字节序列校验接口（当前为 XOR）。
 * 2. 解耦边界：仅提供纯算法函数，不依赖 HAL、Driver、RTOS。
 * 3. 上层调用：通信模块可统一复用本接口生成校验字段。
 * 4. 生命周期：无状态工具函数，调用方自行管理输入缓存。
 */
#ifndef FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H
#define FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H

#include <stdint.h>

/**
 * @brief 计算字节缓冲区 XOR 校验值。
 * @details
 * 该接口采用逐字节异或方式计算校验值，常用于串口协议 BCC/XOR 字段。
 *
 * @param data 输入数据首地址。
 * @param len 输入数据长度（字节）。
 * @return uint8_t XOR 校验结果；当 `data == NULL` 或 `len == 0` 时返回 `0`。
 */
uint8_t common_checksum_xor_u8(const uint8_t *data, uint16_t len);

#endif /* FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H */
