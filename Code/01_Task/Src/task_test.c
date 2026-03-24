/**
 * @file    task_test.c
 * @brief   测试任务实现。
 * @details
 * 1. 文件作用：实现调试/测试流程中的示例逻辑与验证入口。
 * 2. 上下层绑定：上层由调试任务创建调用；下层依赖各模块接口进行联调。
 */
#include "task_test.h"
#include "task_init.h"

/**
 * @brief 调试用浮点限幅函数。
 * @param value 待限幅值。
 * @param min 下限值。
 * @param max 上限值。
 * @return 限幅后的结果。
 */
static float task_test_clamp(float value, float min, float max)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    if (value < min)
    {
        return min;
    }
    if (value > max)
    {
        return max;
    }
    return value;
}

/**
 * @brief 测试任务入口：当前保留为低频空转，注释块内保留调试发送模板。
 * @details
 * 1. 任务通过 `task_wait_init_done` 与系统初始化闸门对齐。
 * 2. 循环内默认仅 `osDelay`，不主动驱动业务控制链路。
 * 3. 已保留 VOFA 发送模板代码，后续可按需解注释启用联调。
 * @param argument 任务参数（未使用）。
 * @return 无。
 */
void StartTestTask(void *argument)
{
    // 1. 执行本函数核心流程，按输入参数更新输出与状态。
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx(); // 调试发送使用的 VOFA 上下文
    float vofa_payload[3]; // 调试发送缓存：[weight, left_speed, right_speed]
    float weight; // 调试用循迹权重
    float correction; // 由权重计算得到的速度修正量
    float left_target_speed; // 左轮目标速度（调试推算）
    float right_target_speed; // 右轮目标速度（调试推算）

    (void)argument;

    task_wait_init_done();

    for (;;) // 循环计数器
    {
    //     weight = mod_sensor_get_weight();
    //     correction = task_test_clamp(weight, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);
    //     left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - correction);
    //     right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + correction);
    //
    //     vofa_payload[0] = weight;
    //     vofa_payload[1] = left_target_speed;
    //     vofa_payload[2] = right_target_speed;
    //
    //     if (mod_vofa_is_bound(p_vofa_ctx))
    //     {
    //         (void)mod_vofa_send_float_ctx(p_vofa_ctx, "WeightSpeed", vofa_payload, 3U);
    //     }

        osDelay(TASK_TEST_PERIOD_MS);
    }
}



