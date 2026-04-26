/**
 * @file    task_debug.h
 * @author  Amber Ge
 * @brief   Debug 任务对外接口。
 */
#ifndef ZGT6_FREERTOS_TASK_DEBUG_H
#define ZGT6_FREERTOS_TASK_DEBUG_H

#include <stdbool.h>

typedef enum
{
    /* 关闭调试输出。调试任务仍然运行，但不会执行任何测试模式。 */
    TASK_DEBUG_MODE_NONE = 0,

    /*
     * 基础 VOFA 计数测试。
     * 用途：
     * 1. 验证调试任务循环是否正常运行。
     * 2. 验证 mod_vofa_send_int() 链路是否能持续发送数据。
     * 3. 观察发送成功/失败计数是否符合预期。
     */
    TASK_DEBUG_MODE_TEST_VOFA_COUNTER,

    /*
     * K230 视觉数据 + VOFA 输出测试。
     * 用途：
     * 1. 验证视觉模块是否持续产出最新识别结果。
     * 2. 验证视觉数据是否超时失效。
     * 3. 同时把 K230 原始协议帧逐字节发到 VOFA，便于排查协议收发问题。
     */
    TASK_DEBUG_MODE_TEST_K230_VOFA,

    /*
     * 板载用户按键 + VOFA 输出测试。
     * 用途：
     * 1. 验证按键 GPIO 读取是否正确。
     * 2. 验证消抖逻辑是否稳定。
     * 3. 验证按下沿统计和单次触发脉冲是否符合预期。
     */
    TASK_DEBUG_MODE_TEST_KEY_VOFA,
    TASK_DEBUG_MODE_MAX
} task_debug_mode_t;

void StartDebugTask(void *argument);
bool task_debug_set_mode(task_debug_mode_t mode);
task_debug_mode_t task_debug_get_mode(void);

#endif /* ZGT6_FREERTOS_TASK_DEBUG_H */
