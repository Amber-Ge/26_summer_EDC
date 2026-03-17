#include "mod_oled.h"
#include "i2c.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OLED_WIDTH         128U // OLED 横向像素数
#define OLED_HEIGHT        64U // OLED 纵向像素数
#define OLED_PAGE_NUM      8U // 页数（每页 8 行像素）
#define OLED_ADDRESS       (0x3CU << 1U) // OLED I2C 设备地址
#define OLED_I2C_TIMEOUT   50U // I2C 发送超时时间（ms）
#define OLED_TX_FRAME_MAX  (OLED_WIDTH + 1U) // 单页发送帧最大长度（控制字节+数据）

uint8_t OLED_DisplayBuf[OLED_PAGE_NUM][OLED_WIDTH]; // OLED 显存缓存（页寻址）

static uint8_t s_oled_tx_frame[OLED_TX_FRAME_MAX]; // I2C 一帧发送缓存（控制字节 + 数据）
static volatile uint8_t s_oled_tx_done = 1U; // 异步发送完成标志
static volatile uint8_t s_oled_tx_error = 0U; // 异步发送错误标志

static HAL_StatusTypeDef OLED_I2C_WaitReady(uint32_t timeout_ms)
{
    uint32_t tick_start = HAL_GetTick(); // 等待起始 tick

    //1. 轮询 I2C 状态直到就绪或超时
    while (HAL_I2C_GetState(&hi2c2) != HAL_I2C_STATE_READY)
    {
        if ((HAL_GetTick() - tick_start) >= timeout_ms)
        {
            return HAL_TIMEOUT;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef OLED_I2C_TransmitFrame(const uint8_t *frame, uint16_t len)
{
    HAL_StatusTypeDef status; // I2C 接口状态
    uint32_t tick_start; // 发送等待起始 tick

    //1. 参数校验
    if ((frame == NULL) || (len == 0U))
    {
        return HAL_ERROR;
    }

    //2. 等待 I2C 外设空闲
    status = OLED_I2C_WaitReady(OLED_I2C_TIMEOUT);
    if (status != HAL_OK)
    {
        return status;
    }

    //3. 清除发送完成/错误标志并启动中断发送
    s_oled_tx_done = 0U;
    s_oled_tx_error = 0U;

    status = HAL_I2C_Master_Transmit_IT(&hi2c2, OLED_ADDRESS, (uint8_t *)frame, len);
    if (status != HAL_OK)
    {
        s_oled_tx_done = 1U;
        return status;
    }

    //4. 等待发送完成标志
    tick_start = HAL_GetTick();
    while (s_oled_tx_done == 0U)
    {
        if ((HAL_GetTick() - tick_start) >= OLED_I2C_TIMEOUT)
        {
            return HAL_TIMEOUT;
        }
    }

    //5. 发送完成后检查错误标志
    if (s_oled_tx_error != 0U)
    {
        return HAL_ERROR;
    }

    return HAL_OK;
}

void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    //1. OLED 对应 I2C 完成回调：置发送完成标志
    if (hi2c == &hi2c2)
    {
        s_oled_tx_done = 1U;
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c)
{
    //1. OLED 对应 I2C 错误回调：置错误与完成标志
    if (hi2c == &hi2c2)
    {
        s_oled_tx_error = 1U;
        s_oled_tx_done = 1U;
    }
}

static void OLED_WriteCommand(uint8_t command)
{
    uint8_t frame[2]; // 指令帧：控制字节 + 指令

    //1. 组装指令帧并发送
    frame[0] = 0x00U;
    frame[1] = command;
    (void)OLED_I2C_TransmitFrame(frame, (uint16_t)sizeof(frame));
}

static void OLED_WriteData(const uint8_t *data, uint8_t count)
{
    //1. 参数校验
    if ((data == NULL) || (count == 0U))
    {
        return;
    }

    //2. 长度限幅
    if (count > OLED_WIDTH)
    {
        count = OLED_WIDTH;
    }

    //3. 组装数据帧（控制字节 0x40）并发送
    s_oled_tx_frame[0] = 0x40U;
    memcpy(&s_oled_tx_frame[1], data, count);
    (void)OLED_I2C_TransmitFrame(s_oled_tx_frame, (uint16_t)count + 1U);
}

static void OLED_SetCursor(uint8_t page, uint8_t x)
{
    //1. 设置页地址与列地址
    OLED_WriteCommand((uint8_t)(0xB0U | (page & 0x07U)));
    OLED_WriteCommand((uint8_t)(0x10U | ((x & 0xF0U) >> 4U)));
    OLED_WriteCommand((uint8_t)(0x00U | (x & 0x0FU)));
}

static uint32_t OLED_Pow(uint32_t x, uint32_t y)
{
    uint32_t result = 1U; // 幂运算结果

    //1. 循环乘法计算 x^y
    while (y-- > 0U)
    {
        result *= x;
    }

    return result;
}

static uint8_t OLED_AngleInRange(int16_t angle, int16_t start_angle, int16_t end_angle)
{
    //1. 判定角度是否在给定扇形范围内（支持跨 0 度）
    if (start_angle <= end_angle)
    {
        return (uint8_t)((angle >= start_angle) && (angle <= end_angle));
    }

    return (uint8_t)((angle >= start_angle) || (angle <= end_angle));
}

static int32_t OLED_TriArea2(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t x3, int16_t y3)
{
    //1. 返回三角形有向面积的 2 倍值，用于点在三角形判定
    return (int32_t)(x2 - x1) * (int32_t)(y3 - y1) - (int32_t)(y2 - y1) * (int32_t)(x3 - x1);
}

static uint8_t OLED_PointInTriangle(int16_t px,
                                    int16_t py,
                                    int16_t x0,
                                    int16_t y0,
                                    int16_t x1,
                                    int16_t y1,
                                    int16_t x2,
                                    int16_t y2)
{
    int32_t c1 = OLED_TriArea2(x0, y0, x1, y1, px, py); // 边 x0y0 -> x1y1 的符号面积
    int32_t c2 = OLED_TriArea2(x1, y1, x2, y2, px, py); // 边 x1y1 -> x2y2 的符号面积
    int32_t c3 = OLED_TriArea2(x2, y2, x0, y0, px, py); // 边 x2y2 -> x0y0 的符号面积

    uint8_t has_neg = (uint8_t)((c1 < 0) || (c2 < 0) || (c3 < 0)); // 是否存在负号
    uint8_t has_pos = (uint8_t)((c1 > 0) || (c2 > 0) || (c3 > 0)); // 是否存在正号

    //1. 三个符号同号或包含零时点在三角形内
    return (uint8_t)(!(has_neg && has_pos));
}

void OLED_Init(void)
{
    static const uint8_t init_cmds[] = {
        0xAEU,
        0xD5U, 0x80U,
        0xA8U, 0x3FU,
        0xD3U, 0x00U,
        0x40U,
        0xA1U,
        0xC8U,
        0xDAU, 0x12U,
        0x81U, 0xCFU,
        0xD9U, 0xF1U,
        0xDBU, 0x30U,
        0xA4U,
        0xA6U,
        0x8DU, 0x14U,
        0xAFU,
    };

    uint32_t i; // 初始化指令索引

    //1. 上电等待后按序发送初始化命令
    HAL_Delay(10U);

    for (i = 0U; i < (uint32_t)sizeof(init_cmds); i++)
    {
        OLED_WriteCommand(init_cmds[i]);
    }

    //2. 清空显存并整屏刷新
    OLED_Clear();
    OLED_Update();
}

void OLED_Update(void)
{
    uint8_t page; // 页索引

    //1. 遍历 8 页并把显存页写入 OLED
    for (page = 0U; page < OLED_PAGE_NUM; page++)
    {
        OLED_SetCursor(page, 0U);
        OLED_WriteData(OLED_DisplayBuf[page], OLED_WIDTH);
    }
}

void OLED_UpdateArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
    uint8_t start_page; // 起始页
    uint8_t end_page; // 结束页
    uint8_t page; // 当前页

    //1. 参数与边界校验
    if ((X >= OLED_WIDTH) || (Y >= OLED_HEIGHT) || (Width == 0U) || (Height == 0U))
    {
        return;
    }

    //2. 裁剪刷新区域到屏幕范围
    if ((uint16_t)X + Width > OLED_WIDTH)
    {
        Width = (uint8_t)(OLED_WIDTH - X);
    }

    if ((uint16_t)Y + Height > OLED_HEIGHT)
    {
        Height = (uint8_t)(OLED_HEIGHT - Y);
    }

    //3. 计算页范围并逐页发送区域数据
    start_page = (uint8_t)(Y / 8U);
    end_page = (uint8_t)((Y + Height - 1U) / 8U);

    for (page = start_page; page <= end_page; page++)
    {
        OLED_SetCursor(page, X);
        OLED_WriteData(&OLED_DisplayBuf[page][X], Width);
    }
}

void OLED_Clear(void)
{
    //1. 清空整屏显存缓存
    memset(OLED_DisplayBuf, 0, sizeof(OLED_DisplayBuf));
}

void OLED_ClearArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
    uint16_t x; // 横向像素索引
    uint16_t y; // 纵向像素索引

    //1. 参数校验与区域裁剪
    if ((X >= OLED_WIDTH) || (Y >= OLED_HEIGHT) || (Width == 0U) || (Height == 0U))
    {
        return;
    }

    if ((uint16_t)X + Width > OLED_WIDTH)
    {
        Width = (uint8_t)(OLED_WIDTH - X);
    }

    if ((uint16_t)Y + Height > OLED_HEIGHT)
    {
        Height = (uint8_t)(OLED_HEIGHT - Y);
    }

    //2. 清零指定区域像素位
    for (y = Y; y < (uint16_t)Y + Height; y++)
    {
        for (x = X; x < (uint16_t)X + Width; x++)
        {
            OLED_DisplayBuf[y / 8U][x] &= (uint8_t)~(1U << (y & 0x07U));
        }
    }
}

void OLED_Reverse(void)
{
    uint8_t page; // 页索引
    uint8_t x; // 列索引

    //1. 全屏按位取反
    for (page = 0U; page < OLED_PAGE_NUM; page++)
    {
        for (x = 0U; x < OLED_WIDTH; x++)
        {
            OLED_DisplayBuf[page][x] ^= 0xFFU;
        }
    }
}

void OLED_ReverseArea(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height)
{
    uint16_t x; // 横向像素索引
    uint16_t y; // 纵向像素索引

    //1. 参数校验与区域裁剪
    if ((X >= OLED_WIDTH) || (Y >= OLED_HEIGHT) || (Width == 0U) || (Height == 0U))
    {
        return;
    }

    if ((uint16_t)X + Width > OLED_WIDTH)
    {
        Width = (uint8_t)(OLED_WIDTH - X);
    }

    if ((uint16_t)Y + Height > OLED_HEIGHT)
    {
        Height = (uint8_t)(OLED_HEIGHT - Y);
    }

    //2. 对目标区域像素执行按位异或翻转
    for (y = Y; y < (uint16_t)Y + Height; y++)
    {
        for (x = X; x < (uint16_t)X + Width; x++)
        {
            OLED_DisplayBuf[y / 8U][x] ^= (uint8_t)(1U << (y & 0x07U));
        }
    }
}

void OLED_ShowChar(uint8_t X, uint8_t Y, char Char, uint8_t FontSize)
{
    uint8_t index; // 字库索引

    //1. 字符范围校验，超出可显示 ASCII 则替换为空格
    if ((Char < ' ') || (Char > '~'))
    {
        Char = ' ';
    }

    index = (uint8_t)(Char - ' ');

    //2. 按字体大小选择字库并绘制
    if (FontSize == OLED_8X16)
    {
        OLED_ShowImage(X, Y, 8U, 16U, OLED_F8x16[index]);
    }
    else if (FontSize == OLED_6X8)
    {
        OLED_ShowImage(X, Y, 6U, 8U, OLED_F6x8[index]);
    }
}

void OLED_ShowString(uint8_t X, uint8_t Y, char *String, uint8_t FontSize)
{
    uint16_t i = 0U; // 字符索引

    //1. 参数校验
    if (String == NULL)
    {
        return;
    }

    //2. 逐字符连续绘制字符串
    while (String[i] != '\0')
    {
        OLED_ShowChar((uint8_t)(X + i * FontSize), Y, String[i], FontSize);
        i++;
    }
}

void OLED_ShowNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i; // 数字位索引

    //1. 从高位到低位依次提取并绘制数字
    for (i = 0U; i < Length; i++)
    {
        uint32_t div = OLED_Pow(10U, (uint32_t)(Length - i - 1U));
        uint8_t digit = (uint8_t)((Number / div) % 10U);
        OLED_ShowChar((uint8_t)(X + i * FontSize), Y, (char)('0' + digit), FontSize);
    }
}

void OLED_ShowSignedNum(uint8_t X, uint8_t Y, int32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint32_t abs_num; // 绝对值部分

    //1. 绘制正负号并计算绝对值
    if (Number >= 0)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
        abs_num = (uint32_t)Number;
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        abs_num = (uint32_t)(-(int64_t)Number);
    }

    //2. 绘制数值部分
    OLED_ShowNum((uint8_t)(X + FontSize), Y, abs_num, Length, FontSize);
}

void OLED_ShowHexNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i; // 十六进制位索引

    //1. 从高位到低位提取 nibble 并绘制
    for (i = 0U; i < Length; i++)
    {
        uint8_t nibble = (uint8_t)((Number >> (4U * (Length - i - 1U))) & 0x0FU);
        if (nibble < 10U)
        {
            OLED_ShowChar((uint8_t)(X + i * FontSize), Y, (char)('0' + nibble), FontSize);
        }
        else
        {
            OLED_ShowChar((uint8_t)(X + i * FontSize), Y, (char)('A' + nibble - 10U), FontSize);
        }
    }
}

void OLED_ShowBinNum(uint8_t X, uint8_t Y, uint32_t Number, uint8_t Length, uint8_t FontSize)
{
    uint8_t i; // 二进制位索引

    //1. 从高位到低位逐位绘制 0/1
    for (i = 0U; i < Length; i++)
    {
        uint8_t bit = (uint8_t)((Number >> (Length - i - 1U)) & 0x01U);
        OLED_ShowChar((uint8_t)(X + i * FontSize), Y, (char)('0' + bit), FontSize);
    }
}

void OLED_ShowFloatNum(uint8_t X,
                       uint8_t Y,
                       double Number,
                       uint8_t IntLength,
                       uint8_t FraLength,
                       uint8_t FontSize)
{
    double abs_num = Number; // 数值绝对值
    uint32_t pow10; // 小数缩放因子
    uint64_t scaled; // 缩放后四舍五入值
    uint32_t int_part; // 整数部分
    uint32_t frac_part; // 小数部分

    //1. 绘制符号位并取绝对值
    if (Number >= 0.0)
    {
        OLED_ShowChar(X, Y, '+', FontSize);
    }
    else
    {
        OLED_ShowChar(X, Y, '-', FontSize);
        abs_num = -Number;
    }

    //2. 分离整数和小数并绘制
    pow10 = OLED_Pow(10U, FraLength);
    scaled = (uint64_t)llround(abs_num * (double)pow10);
    int_part = (uint32_t)(scaled / pow10);
    frac_part = (uint32_t)(scaled % pow10);

    OLED_ShowNum((uint8_t)(X + FontSize), Y, int_part, IntLength, FontSize);
    OLED_ShowChar((uint8_t)(X + (IntLength + 1U) * FontSize), Y, '.', FontSize);
    OLED_ShowNum((uint8_t)(X + (IntLength + 2U) * FontSize), Y, frac_part, FraLength, FontSize);
}

void OLED_ShowChinese(uint8_t X, uint8_t Y, char *Chinese)
{
    uint16_t offset = 0U; // UTF-8 字节偏移
    uint16_t idx = 0U; // 中文字符序号

    //1. 参数校验
    if (Chinese == NULL)
    {
        return;
    }

    //2. 按固定 UTF-8 宽度切分字符并在字库中查找绘制
    while (Chinese[offset] != '\0')
    {
        char single[OLED_CHN_CHAR_WIDTH + 1U] = {0}; // 单个中文字符缓存
        uint16_t map_index = 0U; // 字库索引

        if (Chinese[offset + OLED_CHN_CHAR_WIDTH - 1U] == '\0')
        {
            break;
        }

        memcpy(single, &Chinese[offset], OLED_CHN_CHAR_WIDTH);
        single[OLED_CHN_CHAR_WIDTH] = '\0';

        while (strcmp(OLED_CF16x16[map_index].Index, "") != 0)
        {
            if (strcmp(OLED_CF16x16[map_index].Index, single) == 0)
            {
                break;
            }
            map_index++;
        }

        OLED_ShowImage((uint8_t)(X + idx * 16U), Y, 16U, 16U, OLED_CF16x16[map_index].Data);

        idx++;
        offset += OLED_CHN_CHAR_WIDTH;
    }
}

void OLED_ShowImage(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, const uint8_t *Image)
{
    uint8_t i; // 列索引
    uint8_t page; // 页索引
    uint8_t page_count; // 图像页数
    uint8_t y_offset; // Y 偏移位

    //1. 参数与边界校验
    if ((Image == NULL) || (X >= OLED_WIDTH) || (Y >= OLED_HEIGHT) || (Width == 0U) || (Height == 0U))
    {
        return;
    }

    //2. 裁剪区域并清空目标区域
    if ((uint16_t)X + Width > OLED_WIDTH)
    {
        Width = (uint8_t)(OLED_WIDTH - X);
    }

    if ((uint16_t)Y + Height > OLED_HEIGHT)
    {
        Height = (uint8_t)(OLED_HEIGHT - Y);
    }

    OLED_ClearArea(X, Y, Width, Height);

    //3. 按页将图像数据合并到显存（处理页间跨越）
    page_count = (uint8_t)((Height + 7U) / 8U);
    y_offset = (uint8_t)(Y & 0x07U);

    for (page = 0U; page < page_count; page++)
    {
        uint8_t dst_page = (uint8_t)(Y / 8U + page);
        for (i = 0U; i < Width; i++)
        {
            uint8_t byte = Image[page * Width + i]; // 图像字节数据
            uint8_t x = (uint8_t)(X + i); // 目标列坐标

            if (dst_page >= OLED_PAGE_NUM)
            {
                continue;
            }

            OLED_DisplayBuf[dst_page][x] |= (uint8_t)(byte << y_offset);

            if ((y_offset != 0U) && ((uint8_t)(dst_page + 1U) < OLED_PAGE_NUM))
            {
                OLED_DisplayBuf[dst_page + 1U][x] |= (uint8_t)(byte >> (8U - y_offset));
            }
        }
    }
}

void OLED_Printf(uint8_t X, uint8_t Y, uint8_t FontSize, char *format, ...)
{
    char str[32]; // 格式化输出缓存
    va_list arg; // 可变参数列表

    //1. 参数校验
    if (format == NULL)
    {
        return;
    }

    //2. 格式化字符串后绘制
    va_start(arg, format);
    (void)vsnprintf(str, sizeof(str), format, arg);
    va_end(arg);

    OLED_ShowString(X, Y, str, FontSize);
}

void OLED_DrawPoint(uint8_t X, uint8_t Y)
{
    //1. 边界校验并置位目标像素
    if ((X >= OLED_WIDTH) || (Y >= OLED_HEIGHT))
    {
        return;
    }

    OLED_DisplayBuf[Y / 8U][X] |= (uint8_t)(1U << (Y & 0x07U));
}

uint8_t OLED_GetPoint(uint8_t X, uint8_t Y)
{
    //1. 边界校验，越界返回 0
    if ((X >= OLED_WIDTH) || (Y >= OLED_HEIGHT))
    {
        return 0U;
    }

    //2. 读取目标像素位
    return (uint8_t)((OLED_DisplayBuf[Y / 8U][X] >> (Y & 0x07U)) & 0x01U);
}

void OLED_DrawLine(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1)
{
    int16_t x0 = X0; // 当前 x
    int16_t y0 = Y0; // 当前 y
    int16_t x1 = X1; // 终点 x
    int16_t y1 = Y1; // 终点 y
    int16_t dx = (int16_t)abs(x1 - x0); // x 方向差值
    int16_t sx = (x0 < x1) ? 1 : -1; // x 方向步进
    int16_t dy = (int16_t)-abs(y1 - y0); // y 方向差值（负值）
    int16_t sy = (y0 < y1) ? 1 : -1; // y 方向步进
    int16_t err = dx + dy; // 误差项

    //1. Bresenham 算法逐点绘制直线
    while (1)
    {
        OLED_DrawPoint((uint8_t)x0, (uint8_t)y0);

        if ((x0 == x1) && (y0 == y1))
        {
            break;
        }

        int16_t e2 = (int16_t)(2 * err); // 误差放大值
        if (e2 >= dy)
        {
            err = (int16_t)(err + dy);
            x0 = (int16_t)(x0 + sx);
        }
        if (e2 <= dx)
        {
            err = (int16_t)(err + dx);
            y0 = (int16_t)(y0 + sy);
        }
    }
}

void OLED_DrawRectangle(uint8_t X, uint8_t Y, uint8_t Width, uint8_t Height, uint8_t IsFilled)
{
    uint16_t x; // 横向像素索引
    uint16_t y; // 纵向像素索引

    //1. 参数校验
    if ((Width == 0U) || (Height == 0U))
    {
        return;
    }

    //2. 按填充模式绘制矩形
    if (IsFilled == OLED_FILLED)
    {
        for (x = X; x < (uint16_t)X + Width; x++)
        {
            for (y = Y; y < (uint16_t)Y + Height; y++)
            {
                OLED_DrawPoint((uint8_t)x, (uint8_t)y);
            }
        }
    }
    else
    {
        OLED_DrawLine(X, Y, (uint8_t)(X + Width - 1U), Y);
        OLED_DrawLine(X, (uint8_t)(Y + Height - 1U), (uint8_t)(X + Width - 1U), (uint8_t)(Y + Height - 1U));
        OLED_DrawLine(X, Y, X, (uint8_t)(Y + Height - 1U));
        OLED_DrawLine((uint8_t)(X + Width - 1U), Y, (uint8_t)(X + Width - 1U), (uint8_t)(Y + Height - 1U));
    }
}

void OLED_DrawTriangle(uint8_t X0, uint8_t Y0, uint8_t X1, uint8_t Y1, uint8_t X2, uint8_t Y2, uint8_t IsFilled)
{
    //1. 填充模式：边界框内逐点判定是否在三角形内部
    if (IsFilled == OLED_FILLED)
    {
        uint8_t min_x = X0;
        uint8_t max_x = X0;
        uint8_t min_y = Y0;
        uint8_t max_y = Y0;
        uint16_t x;
        uint16_t y;

        if (X1 < min_x) { min_x = X1; }
        if (X2 < min_x) { min_x = X2; }
        if (X1 > max_x) { max_x = X1; }
        if (X2 > max_x) { max_x = X2; }

        if (Y1 < min_y) { min_y = Y1; }
        if (Y2 < min_y) { min_y = Y2; }
        if (Y1 > max_y) { max_y = Y1; }
        if (Y2 > max_y) { max_y = Y2; }

        for (x = min_x; x <= max_x; x++)
        {
            for (y = min_y; y <= max_y; y++)
            {
                if (OLED_PointInTriangle((int16_t)x, (int16_t)y, X0, Y0, X1, Y1, X2, Y2) != 0U)
                {
                    OLED_DrawPoint((uint8_t)x, (uint8_t)y);
                }
            }
        }
    }
    //2. 非填充模式：绘制三条边
    else
    {
        OLED_DrawLine(X0, Y0, X1, Y1);
        OLED_DrawLine(X1, Y1, X2, Y2);
        OLED_DrawLine(X2, Y2, X0, Y0);
    }
}

void OLED_DrawCircle(uint8_t X, uint8_t Y, uint8_t Radius, uint8_t IsFilled)
{
    //1. 填充模式：按 y 扫描并计算左右边界
    if (IsFilled == OLED_FILLED)
    {
        int16_t dy;
        for (dy = -(int16_t)Radius; dy <= (int16_t)Radius; dy++)
        {
            int16_t dx = (int16_t)sqrt((double)((int32_t)Radius * Radius - (int32_t)dy * dy));
            int16_t x;
            for (x = (int16_t)X - dx; x <= (int16_t)X + dx; x++)
            {
                OLED_DrawPoint((uint8_t)x, (uint8_t)((int16_t)Y + dy));
            }
        }
    }
    //2. 非填充模式：中点圆算法绘制 8 对称点
    else
    {
        int16_t x = 0;
        int16_t y = Radius;
        int16_t d = (int16_t)(1 - Radius);

        while (x <= y)
        {
            OLED_DrawPoint((uint8_t)(X + x), (uint8_t)(Y + y));
            OLED_DrawPoint((uint8_t)(X + y), (uint8_t)(Y + x));
            OLED_DrawPoint((uint8_t)(X - x), (uint8_t)(Y + y));
            OLED_DrawPoint((uint8_t)(X - y), (uint8_t)(Y + x));
            OLED_DrawPoint((uint8_t)(X + x), (uint8_t)(Y - y));
            OLED_DrawPoint((uint8_t)(X + y), (uint8_t)(Y - x));
            OLED_DrawPoint((uint8_t)(X - x), (uint8_t)(Y - y));
            OLED_DrawPoint((uint8_t)(X - y), (uint8_t)(Y - x));

            x++;
            if (d < 0)
            {
                d = (int16_t)(d + 2 * x + 1);
            }
            else
            {
                y--;
                d = (int16_t)(d + 2 * (x - y) + 1);
            }
        }
    }
}

void OLED_DrawEllipse(uint8_t X, uint8_t Y, uint8_t A, uint8_t B, uint8_t IsFilled)
{
    int16_t dy; // y 偏移量

    //1. 参数校验
    if ((A == 0U) || (B == 0U))
    {
        return;
    }

    //2. 填充模式：按 y 扫描，计算每行 x 半径
    if (IsFilled == OLED_FILLED)
    {
        for (dy = -(int16_t)B; dy <= (int16_t)B; dy++)
        {
            double t = 1.0 - ((double)dy * (double)dy) / ((double)B * (double)B);
            if (t < 0.0)
            {
                continue;
            }
            int16_t dx = (int16_t)lround((double)A * sqrt(t));
            int16_t x;
            for (x = (int16_t)X - dx; x <= (int16_t)X + dx; x++)
            {
                OLED_DrawPoint((uint8_t)x, (uint8_t)((int16_t)Y + dy));
            }
        }
    }
    //3. 非填充模式：按角度采样绘制椭圆边界
    else
    {
        int16_t angle;
        for (angle = -180; angle <= 180; angle++)
        {
            double rad = (double)angle * M_PI / 180.0;
            int16_t px = (int16_t)lround((double)X + (double)A * cos(rad));
            int16_t py = (int16_t)lround((double)Y + (double)B * sin(rad));
            OLED_DrawPoint((uint8_t)px, (uint8_t)py);
        }
    }
}

void OLED_DrawArc(uint8_t X,
                  uint8_t Y,
                  uint8_t Radius,
                  int16_t StartAngle,
                  int16_t EndAngle,
                  uint8_t IsFilled)
{
    int16_t r_start = (IsFilled == OLED_FILLED) ? 0 : (int16_t)Radius; // 起始半径
    int16_t r; // 当前半径

    //1. 按半径与角度扫描绘制扇形/圆弧
    for (r = r_start; r <= (int16_t)Radius; r++)
    {
        int16_t angle;
        for (angle = -180; angle <= 180; angle++)
        {
            if (OLED_AngleInRange(angle, StartAngle, EndAngle) != 0U)
            {
                double rad = (double)angle * M_PI / 180.0;
                int16_t px = (int16_t)lround((double)X + (double)r * cos(rad));
                int16_t py = (int16_t)lround((double)Y + (double)r * sin(rad));
                OLED_DrawPoint((uint8_t)px, (uint8_t)py);
            }
        }
    }
}
