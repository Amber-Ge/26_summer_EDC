/**
 ******************************************************************************
 * @file    common_checksum.h
 * @brief   通用校验工具接口定义
 ******************************************************************************
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
