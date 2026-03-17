#include "task_dcc.h"
#include "mod_vofa.h"

// 浮点限幅函数：将输入值约束到 [min, max] 区间
static float clamp_float(float value, float min, float max)
{
    float result = value; // 1. 默认输出原始值
    if (result < min) // 2. 下限保护
        result = min;
    if (result > max) // 3. 上限保护
        result = max;
    return result; // 4. 统一出口
}

// 浮点占空比转 int16 指令：四舍五入并执行安全限幅
static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited; // 限幅后的浮点占空比

    // 1. 限制到电机驱动允许范围
    duty_limited = clamp_float(duty_f, -(float)MOD_MOTOR_DUTY_MAX, (float)MOD_MOTOR_DUTY_MAX);

    // 2. 执行四舍五入（兼容正负值）
    if (duty_limited >= 0.0f)
        duty_limited += 0.5f;
    else
        duty_limited -= 0.5f;

    // 3. 转换为电机接口所需的有符号占空比
    return (int16_t)duty_limited;
}

void StartDccTask(void *argument)
{
    pid_pos_t pos_pid; // 外环位置式 PID（左右轮累计里程差）
    pid_inc_t left_speed_pid; // 左轮内环速度 PID
    pid_inc_t right_speed_pid; // 右轮内环速度 PID
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx(); // VOFA 默认上下文指针

    uint8_t control_mode = TASK_DCC_MODE_BALANCE; // 控制模式：0=双环平衡，1=循迹预留
    (void)argument; // 任务参数当前未使用

    // 1. 初始化底盘电机模块并切换到驱动模式
    mod_motor_init();
    mod_motor_set_mode(MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);

    // 2. 初始化外环位置 PID：目标为左右轮累计里程差 = MOTOR_TARGET_ERROR（默认 0）
    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

    // 3. 初始化内环速度 PID：左右轮各自一套增量式速度控制器
    PID_Inc_Init(&left_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);
    PID_Inc_Init(&right_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    osDelay(3000);
    // 4. 主循环：固定 20ms 执行一次（50Hz）
    for (;;)
    {
        // 4.1 非阻塞查询模式切换信号：KEY3 触发后切换控制模式
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            // 4.1.1 模式翻转：0 <-> 1
            control_mode ^= 1U;

            // 4.1.2 切换模式时复位 PID 状态，防止历史误差污染
            PID_Pos_Reset(&pos_pid);
            PID_Inc_Reset(&left_speed_pid);
            PID_Inc_Reset(&right_speed_pid);
            PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

            // 4.1.3 切换瞬间先清零输出，保证模式切换平滑和安全
            mod_motor_set_duty(MOD_MOTOR_LEFT, 0);
            mod_motor_set_duty(MOD_MOTOR_RIGHT, 0);
        }

        // 4.2 先刷新底盘编码器速度与累计位置
        mod_motor_tick();

        // 4.3 模式0：外环位置式 + 内环速度式
        if (control_mode == TASK_DCC_MODE_BALANCE)
        {
            int64_t left_pos; // 左轮累计编码器总值
            int64_t right_pos; // 右轮累计编码器总值
            float pos_error; // 你定义的位置误差：左总值 - 右总值
            float outer_output; // 外环输出（速度差分比例）

            float left_target_speed; // 左轮目标速度
            float right_target_speed; // 右轮目标速度
            float left_feedback_speed; // 左轮反馈速度
            float right_feedback_speed; // 右轮反馈速度

            float left_duty_f; // 左轮内环输出（浮点占空比）
            float right_duty_f; // 右轮内环输出（浮点占空比）
            float vofa_payload[4]; // VOFA 上报数组：目标速度、左轮、右轮、位置差

            // 4.3.1 读取左右轮累计位置并计算误差
            left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
            right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
            pos_error = (float)(left_pos - right_pos);

            // 4.3.2 计算外环输出
            // 说明：PID 内部误差定义为 (target - measure)，
            // 这里用 measure = -pos_error，使内部误差等于你要求的 (left_pos - right_pos)
            outer_output = PID_Pos_Compute(&pos_pid, -pos_error);
            outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

            // 4.3.3 按你的公式生成左右轮目标速度
            left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
            right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

            // 4.3.4 读取左右轮速度反馈
            left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
            right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

            // 4.3.5 内环速度 PID 输出占空比命令
            PID_Inc_SetTarget(&left_speed_pid, left_target_speed);
            PID_Inc_SetTarget(&right_speed_pid, right_target_speed);
            left_duty_f = PID_Inc_Compute(&left_speed_pid, left_feedback_speed);
            right_duty_f = PID_Inc_Compute(&right_speed_pid, right_feedback_speed);

            // 4.3.6 下发电机占空比
            mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
            mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));

            // // 4.3.7 发送 VOFA 数据：目标速度、左轮目标、右轮目标、左右总编码器差
            // if (mod_vofa_is_bound(p_vofa_ctx))
            // {
            //     vofa_payload[0] = (float)MOTOR_TARGET_SPEED;
            //     vofa_payload[1] = left_target_speed;
            //     vofa_payload[2] = right_target_speed;
            //     vofa_payload[3] = pos_error;
            //     (void)mod_vofa_send_float_ctx(p_vofa_ctx, "DccCtrl", vofa_payload, 4U);
            // }
        }
        else
        {
            // 4.4 模式1：循迹模式预留（当前按你的要求保持空白）
        }

        // 4.5 固定调度周期：20ms
        osDelay(TASK_DCC_PERIOD_MS);
    }
}
