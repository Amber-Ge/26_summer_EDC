/**
 * @file    mod_oled.h
 * @author  姜凯中
 * @version v1.0.0
 * @date    2026-03-23
 * @brief   OLED 显示模块接口。
 * @details
 * 1. 文件作用：提供 OLED 显存缓存、绘图/文本渲染和整屏刷新接口。
 * 2. 解耦边界：本模块只处理显示通道与渲染原语，不负责页面业务状态判定。
 * 3. 上层绑定：`OledTask` 负责组装页面内容并按节拍调用刷新接口。
 * 4. 下层依赖：I2C 句柄通过绑定接口注入，字模与位图资源由 `mod_oled_data` 提供。
 * 5. 生命周期：先完成 I2C 绑定和 `OLED_Init`，再进入“写缓冲 -> Update”循环。
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

/* OLED常用I2C配置推荐值（由上层决定是否采用） */
#define OLED_I2C_ADDR_DEFAULT       (0x3CU << 1U)
#define OLED_I2C_TIMEOUT_MS_DEFAULT (50U)

/**
 * @brief 绑定OLED通信使用的I2C句柄和通信参数
 * @param hi2c       I2C句柄指针
 * @param dev_addr   设备地址（7bit地址左移1位后的格式）
 * @param timeout_ms 超时时间（毫秒）
 * @return true  绑定成功
 * @return false 绑定失败（参数非法）
 */
bool OLED_BindI2C(I2C_HandleTypeDef *hi2c, uint16_t dev_addr, uint32_t timeout_ms);

/**
 * @brief 解绑OLED当前I2C资源
 */
void OLED_UnbindI2C(void);

/**
 * @brief 查询OLED模块是否已完成I2C绑定
 * @return true 已绑定
 * @return false 未绑定
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
void OLED_Printf(uint8_t X, uint8_t Y, uint8_t FontSize, char *format, ...);

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

