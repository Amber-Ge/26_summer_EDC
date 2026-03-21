#include "task_test.h"
#include "task_init.h"

/**
 * @brief 限幅函数
 */
static float task_test_clamp(float value, float min, float max)
{
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
 * @brief 测试任务：每 1 秒发送权重与左右目标速度
 * @details
 * 1. 读取循迹权重值 weight；
 * 2. 参考 DCC 目标速度分配公式计算左右目标速度；
 * 3. 仅做目标值推算，不调用 PID 计算。
 */
void StartTestTask(void *argument)
{
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx();
    float vofa_payload[3];
    float weight;
    float correction;
    float left_target_speed;
    float right_target_speed;

    (void)argument;

    task_wait_init_done();

    for (;;)
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
