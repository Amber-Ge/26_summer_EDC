/**
* @file    mod_oled_data.h
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   OLED 字库与位图数据接口定义。
 * @details
 * 1. 文件作用：集中声明 OLED 渲染所需的 ASCII/中文字模与位图常量表。
 * 2. 解耦边界：本文件仅承载静态资源描述，不包含任何显示流程或总线访问逻辑。
 * 3. 上层绑定：由 `mod_oled` 在字符绘制与图片显示时按索引读取。
 * 4. 下层依赖：无运行期硬件依赖，仅依赖编译期常量存储。
 * 5. 生命周期：资源在程序全生命周期只读常驻，可被多处显示逻辑复用。
 */
#ifndef FINAL_GRADUATE_WORK_MOD_OLED_DATA_H
#define FINAL_GRADUATE_WORK_MOD_OLED_DATA_H // 头文件防重复包含宏

#include <stdint.h>

/** 中文索引字符串宽度 */
#define OLED_CHN_CHAR_WIDTH 3U // 中文索引字符串宽度

/**
 * @brief 中文字模单元结构体。
 */
typedef struct
{
    char Index[OLED_CHN_CHAR_WIDTH + 1U]; // 中文字符索引（含字符串结束符）
    uint8_t Data[32U]; // 16x16 点阵字模数据
} ChineseCell_t;

/** 8x16 ASCII 字模表 */
extern const uint8_t OLED_F8x16[][16];
/** 6x8 ASCII 字模表 */
extern const uint8_t OLED_F6x8[][6];
/** 16x16 中文字模表 */
extern const ChineseCell_t OLED_CF16x16[];
/** 示例位图数据 */
extern const uint8_t Diode[];

#endif /* FINAL_GRADUATE_WORK_MOD_OLED_DATA_H */


