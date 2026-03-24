/**
 * @file    mod_oled.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   OLED 显示模块接口声明。
 * @details
 * 1. 模块职责：维护显存缓存并提供文本/图形渲染接口。
 * 2. 解耦边界：任务层只组织页面内容与刷新节拍，不直接访问 I2C。
 * 3. 依赖关系：I2C 句柄由绑定接口注入，字库与位图资源来自 `mod_oled_data`。
 * 4. 生命周期：`OLED_BindI2C -> OLED_Init -> 绘制接口 -> OLED_Update`。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_OLED_H
#define FINAL_GRADUATE_WORK_MOD_OLED_H

#include <stdbool.h>
#include <stdint.h>

#include "main.h"
#include "mod_oled_data.h"

#define OLED_8X16      8U
#define OLED_6X8       6U

#define OLED_UNFILLED  0U
#define OLED_FILLED    1U

/* OLED 常用 I2C 默认配置（上层可按需覆盖）。 */
#define OLED_I2C_ADDR_DEFAULT       (0x3CU << 1U)
#define OLED_I2C_TIMEOUT_MS_DEFAULT (50U)

/**
 * @brief 绑定 OLED 通信使用的 I2C 句柄和参数。
 * @param hi2c I2C 句柄指针。
 * @param dev_addr 设备地址（7bit 左移 1 位格式）。
 * @param timeout_ms 发送超时（毫秒）。
 * @return true 绑定成功。
 * @return false 参数非法。
 */
bool OLED_BindI2C(I2C_HandleTypeDef *hi2c, uint16_t dev_addr, uint32_t timeout_ms);

/**
 * @brief 解绑 OLED 当前 I2C 资源。
 */
void OLED_UnbindI2C(void);

/**
 * @brief 查询 OLED 模块是否已完成 I2C 绑定。
 * @return true 已绑定。
 * @return false 未绑定。
 */
bool OLED_IsBoundI2C(void);

/* 基础显示控制 */
void OLED_Init(void);
void OLED_Update(void);
void OLED_UpdateArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);
void OLED_Clear(void);
void OLED_ClearArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);
void OLED_Reverse(void);
void OLED_ReverseArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);

/* 文本与图像显示 */
void OLED_ShowChar(uint8_t X, uint8_t Y, char Char, uint8_t FontSize);
void OLED_ShowString(uint8_t X, uint8_t Y, char *String, uint8_t FontSize);
void OLED_ShowNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowSignedNum(uint8_t X, uint8_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowHexNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowBinNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);
void OLED_ShowFloatNum(uint8_t X, uint8_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);
void OLED_ShowChinese(uint8_t X, uint8_t Y, char *Chinese);
void OLED_ShowImage(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);

/* 图形绘制 */
void OLED_DrawPoint(uint8_t X, uint8_t Y);
uint8_t OLED_GetPoint(uint8_t X, uint8_t Y);
void OLED_DrawLine(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1);
void OLED_DrawRectangle(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);
void OLED_DrawTriangle(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1, uint8_t X2, uint8_t Y2, uint8_t IsFilled);
void OLED_DrawCircle(uint8_t X, uint8_t Y, uint8_t Radius, uint8_t IsFilled);
void OLED_DrawEllipse(uint8_t X, uint8_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);
void OLED_DrawArc(uint8_t X, uint8_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);

#endif /* FINAL_GRADUATE_WORK_MOD_OLED_H */
