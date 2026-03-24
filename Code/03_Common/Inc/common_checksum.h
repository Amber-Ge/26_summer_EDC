/**
 * @file    common_checksum.h
 * @brief   通用校验工具接口定义。
 * @details
 * 1. 文件作用：声明字节流校验算法接口（当前为 XOR），供协议模块复用。
 * 2. 解耦边界：仅提供纯算法函数，不依赖外设驱动和 RTOS 对象。
 * 3. 上层绑定：`mod_k230`、`mod_vofa` 等通信模块调用该接口进行帧校验。
 * 4. 生命周期：无状态纯函数，可在任意上下文直接调用。
 */
#ifndef FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H
#define FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H // 头文件防重复包含宏

#include <stdint.h>

/**
 * @brief 计算字节缓冲区 XOR 校验值。
 * @details
 * 常用于串口协议中的 BCC/XOR 校验字段计算。
 *
 * @param data 输入数据首地址。
 * @param len 输入数据长度（字节）。
 * @return uint8_t 计算得到的 XOR 校验结果。
 */
uint8_t common_checksum_xor_u8(const uint8_t *data, uint16_t len);

#endif /* FINAL_GRADUATE_WORK_COMMON_CHECKSUM_H */
