/**
 * @file    task_test.c
 * @author  姜凯中
 * @version v1.00
 * @date    2026-03-24
 * @brief   测试任务实现。
 * @details
 * 1. 当前版本保持低频空转，不参与主业务控制链路。
 * 2. 文件内保留 VOFA 调试模板，便于联调阶段快速启用。
 * 3. 所有测试逻辑默认关闭，确保不会影响正式任务时序。
 */

#include "task_test.h"
#include "task_init.h"

/**
 * @brief 调试用浮点限幅函数。
 * @param value 待限幅值。
 * @param min 下限。
 * @param max 上限。
 * @return 限幅后的结果。
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
 * @brief TestTask 入口。
 * @details
 * 1. 任务通过 task_wait_init_done 与初始化流程对齐。
 * 2. 默认仅保留延时空转；如需联调，可启用下方注释模板。
 * @param argument RTOS 任务参数（当前未使用）。
 */
void StartTestTask(void *argument)
{
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx(); /* VOFA 默认上下文（调试模板用） */
    float vofa_payload[3];          /* 预留：VOFA 发送缓存 */
    float weight;                   /* 预留：传感器权重 */
    float correction;               /* 预留：限幅修正量 */
    float left_target_speed;        /* 预留：左轮目标速度 */
    float right_target_speed;       /* 预留：右轮目标速度 */

    (void)argument;
    (void)task_test_clamp;
    (void)p_vofa_ctx;
    (void)vofa_payload;
    (void)weight;
    (void)correction;
    (void)left_target_speed;
    (void)right_target_speed;

    /* 步骤1：等待 InitTask 完成资源绑定。 */
    task_wait_init_done();

#if (TASK_TEST_STARTUP_ENABLE == 0U)
    /* 启动开关关闭：挂起测试任务，避免任何调试逻辑被调度。 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    for (;;)
    {
        /*
         * 步骤2（默认关闭）：联调模板示例。
         * 1. 读取循迹权重并计算左右目标速度。
         * 2. 通过 VOFA 输出权重与目标速度曲线。
         * 3. 取消注释后可用于快速联调，不影响当前正式业务链路。
         */
        // weight = mod_sensor_get_weight(mod_sensor_get_default_ctx());
        // correction = task_test_clamp(weight, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);
        // left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - correction);
        // right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + correction);
        //
        // vofa_payload[0] = weight;
        // vofa_payload[1] = left_target_speed;
        // vofa_payload[2] = right_target_speed;
        //
        // if (mod_vofa_is_bound(p_vofa_ctx))
        // {
        //     (void)mod_vofa_send_float_ctx(p_vofa_ctx, "WeightSpeed", vofa_payload, 3U);
        // }

        /* 步骤3：默认低频空转，确保 TestTask 不干扰主控制链路。 */
        osDelay(TASK_TEST_PERIOD_MS);
    }
}

