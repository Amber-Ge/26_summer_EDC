/**
 ******************************************************************************
 * @file    mod_oled.h
 * @brief   OLED 显示模块接口定义
 * @details
 * 提供 OLED 初始化、缓冲区更新、文本显示和图形绘制接口。
 ******************************************************************************
 */
#ifndef FINAL_GRADUATE_WORK_MOD_OLED_H
#define FINAL_GRADUATE_WORK_MOD_OLED_H // 头文件防重复包含宏

#include <stdint.h>
#include "mod_oled_data.h"

#define OLED_8X16      8U // 8x16 字体宽度基准
#define OLED_6X8       6U // 6x8 字体宽度基准

#define OLED_UNFILLED  0U // 图形不填充模式
#define OLED_FILLED    1U // 图形填充模式

/**
 * @brief 初始化 OLED 硬件与显示缓冲区。
 */
void OLED_Init(void);

/**
 * @brief 刷新整屏显示缓冲到 OLED。
 */
void OLED_Update(void);

/**
 * @brief 刷新指定区域到 OLED。
 * @param X 起始 X 坐标。
 * @param Y 起始 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 */
void OLED_UpdateArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);

/**
 * @brief 清空整屏缓冲区。
 */
void OLED_Clear(void);

/**
 * @brief 清空指定区域缓冲区。
 * @param X 起始 X 坐标。
 * @param Y 起始 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 */
void OLED_ClearArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);

/**
 * @brief 整屏反显。
 */
void OLED_Reverse(void);

/**
 * @brief 指定区域反显。
 * @param X 起始 X 坐标。
 * @param Y 起始 Y 坐标。
 * @param Width 区域宽度。
 * @param Height 区域高度。
 */
void OLED_ReverseArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height);

/**
 * @brief 显示单个字符。
 * @param X 显示 X 坐标。
 * @param Y 显示 Y 坐标。
 * @param Char 字符内容。
 * @param FontSize 字体大小（如 `OLED_6X8` / `OLED_8X16`）。
 */
void OLED_ShowChar(uint8_t X, uint8_t Y, char Char, uint8_t FontSize);

/**
 * @brief 显示字符串。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param String 字符串首地址。
 * @param FontSize 字体大小。
 */
void OLED_ShowString(uint8_t X, uint8_t Y, char *String, uint8_t FontSize);

/**
 * @brief 显示无符号十进制数。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Number 数值。
 * @param Length 显示位数。
 * @param FontSize 字体大小。
 */
void OLED_ShowNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 显示有符号十进制数。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Number 数值。
 * @param Length 显示位数。
 * @param FontSize 字体大小。
 */
void OLED_ShowSignedNum(uint8_t X, uint8_t Y, int32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 显示十六进制数。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Number 数值。
 * @param Length 显示位数。
 * @param FontSize 字体大小。
 */
void OLED_ShowHexNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 显示二进制数。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Number 数值。
 * @param Length 显示位数。
 * @param FontSize 字体大小。
 */
void OLED_ShowBinNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize);

/**
 * @brief 显示浮点数。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Number 数值。
 * @param IntLength 整数部分位数。
 * @param FraLength 小数部分位数。
 * @param FontSize 字体大小。
 */
void OLED_ShowFloatNum(uint8_t X, uint8_t Y, double Number, uint8_t IntLength, uint8_t FraLength, uint8_t FontSize);

/**
 * @brief 显示中文字符（字库索引）。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Chinese 中文索引字符串。
 */
void OLED_ShowChinese(uint8_t X, uint8_t Y, char *Chinese);

/**
 * @brief 显示位图图像。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param Width 图像宽度。
 * @param Height 图像高度。
 * @param Image 图像数据首地址。
 */
void OLED_ShowImage(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image);

/**
 * @brief OLED 格式化打印接口。
 * @param X 显示起始 X 坐标。
 * @param Y 显示起始 Y 坐标。
 * @param FontSize 字体大小。
 * @param format 格式化字符串。
 * @param ... 可变参数列表。
 */
void OLED_Printf(uint8_t X, uint8_t Y, uint8_t FontSize, char *format, ...);

/**
 * @brief 绘制单点。
 * @param X 点 X 坐标。
 * @param Y 点 Y 坐标。
 */
void OLED_DrawPoint(uint8_t X, uint8_t Y);

/**
 * @brief 获取指定点像素状态。
 * @param X 点 X 坐标。
 * @param Y 点 Y 坐标。
 * @return uint8_t 点亮状态（0 灭，非 0 亮）。
 */
uint8_t OLED_GetPoint(uint8_t X, uint8_t Y);

/**
 * @brief 绘制直线。
 * @param X0 起点 X 坐标。
 * @param Y0 起点 Y 坐标。
 * @param X1 终点 X 坐标。
 * @param Y1 终点 Y 坐标。
 */
void OLED_DrawLine(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1);

/**
 * @brief 绘制矩形。
 * @param X 左上角 X 坐标。
 * @param Y 左上角 Y 坐标。
 * @param Width 宽度。
 * @param Height 高度。
 * @param IsFilled 是否填充（`OLED_FILLED` / `OLED_UNFILLED`）。
 */
void OLED_DrawRectangle(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled);

/**
 * @brief 绘制三角形。
 * @param X0 顶点0 X 坐标。
 * @param Y0 顶点0 Y 坐标。
 * @param X1 顶点1 X 坐标。
 * @param Y1 顶点1 Y 坐标。
 * @param X2 顶点2 X 坐标。
 * @param Y2 顶点2 Y 坐标。
 * @param IsFilled 是否填充。
 */
void OLED_DrawTriangle(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1, uint8_t X2, uint8_t Y2, uint8_t IsFilled);

/**
 * @brief 绘制圆形。
 * @param X 圆心 X 坐标。
 * @param Y 圆心 Y 坐标。
 * @param Radius 半径。
 * @param IsFilled 是否填充。
 */
void OLED_DrawCircle(uint8_t X, uint8_t Y, uint8_t Radius, uint8_t IsFilled);

/**
 * @brief 绘制椭圆。
 * @param X 椭圆中心 X 坐标。
 * @param Y 椭圆中心 Y 坐标。
 * @param A 长半轴。
 * @param B 短半轴。
 * @param IsFilled 是否填充。
 */
void OLED_DrawEllipse(uint8_t X, uint8_t Y, uint8_t A, uint8_t B, uint8_t IsFilled);

/**
 * @brief 绘制圆弧。
 * @param X 圆心 X 坐标。
 * @param Y 圆心 Y 坐标。
 * @param Radius 半径。
 * @param StartAngle 起始角度（度）。
 * @param EndAngle 结束角度（度）。
 * @param IsFilled 是否填充。
 */
void OLED_DrawArc(uint8_t X, uint8_t Y, uint8_t Radius, int16_t StartAngle, int16_t EndAngle, uint8_t IsFilled);

#endif /* FINAL_GRADUATE_WORK_MOD_OLED_H */
