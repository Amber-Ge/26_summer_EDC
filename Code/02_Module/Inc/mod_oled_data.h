/**
 ******************************************************************************
 * @file    mod_oled_data.h
 * @brief   OLED 字库与位图数据接口定义
 ******************************************************************************
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
