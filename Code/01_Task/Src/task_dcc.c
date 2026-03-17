#include "task_dcc.h"
#include "mod_vofa.h"
#include "tim.h"

/* 底盘电机硬件绑定表：任务启动时显式绑定到模块层 */
static const mod_motor_hw_cfg_t s_motor_bind_map[MOD_MOTOR_MAX] =
{
    [MOD_MOTOR_LEFT] = {
        .in1_port = AIN2_GPIO_Port,
        .in1_pin = AIN2_Pin,
        .in2_port = AIN1_GPIO_Port,
        .in2_pin = AIN1_Pin,
        .pwm_htim = &htim4,
        .pwm_channel = TIM_CHANNEL_1,
        .pwm_invert = false,
        .enc_htim = &htim2,
        .enc_counter_bits = DRV_ENCODER_BITS_32,
        .enc_invert = false,
    },
    [MOD_MOTOR_RIGHT] = {
        .in1_port = BIN1_GPIO_Port,
        .in1_pin = BIN1_Pin,
        .in2_port = BIN2_GPIO_Port,
        .in2_pin = BIN2_Pin,
        .pwm_htim = &htim4,
        .pwm_channel = TIM_CHANNEL_2,
        .pwm_invert = false,
        .enc_htim = &htim3,
        .enc_counter_bits = DRV_ENCODER_BITS_16,
        .enc_invert = true,
    },
};

/* 循迹传感器硬件绑定表：任务启动时显式绑定到模块层 */
static const mod_sensor_map_item_t s_sensor_bind_map[MOD_SENSOR_CHANNEL_NUM] =
{
    {GPIOG, GPIO_PIN_0,  -0.60f},
    {GPIOG, GPIO_PIN_1,  -0.40f},
    {GPIOG, GPIO_PIN_5,  -0.30f},
    {GPIOG, GPIO_PIN_6,  -0.20f},
    {GPIOG, GPIO_PIN_7,  -0.10f},
    {GPIOG, GPIO_PIN_8,  -0.05f},
    {GPIOG, GPIO_PIN_9,   0.05f},
    {GPIOG, GPIO_PIN_10,  0.10f},
    {GPIOG, GPIO_PIN_11,  0.20f},
    {GPIOG, GPIO_PIN_12,  0.30f},
    {GPIOG, GPIO_PIN_13,  0.40f},
    {GPIOG, GPIO_PIN_14,  0.60f}
};

// 浮点限幅函数：将输入值约束到 [min, max] 区间
static float clamp_float(float value, float min, float max)
{
    float result = value;
    if (result < min)
    {
        result = min;
    }
    if (result > max)
    {
        result = max;
    }
    return result;
}

// 浮点占空比转 int16 指令：四舍五入并执行安全限幅
static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited;

    // 1. 限制到电机驱动允许范围。
    duty_limited = clamp_float(duty_f, -(float)MOD_MOTOR_DUTY_MAX, (float)MOD_MOTOR_DUTY_MAX);

    // 2. 执行四舍五入（兼容正负值）。
    if (duty_limited >= 0.0f)
    {
        duty_limited += 0.5f;
    }
    else
    {
        duty_limited -= 0.5f;
    }

    // 3. 转换为电机接口所需的有符号占空比。
    return (int16_t)duty_limited;
}

void StartDccTask(void *argument)
{
    pid_pos_t pos_pid;
    pid_inc_t left_speed_pid;
    pid_inc_t right_speed_pid;
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx();

    uint8_t control_mode = TASK_DCC_MODE_BALANCE;
    (void)argument;

    // 1. 显式绑定电机和传感器模块（无默认回落）。
    (void)mod_motor_bind_map(s_motor_bind_map, MOD_MOTOR_MAX);
    (void)mod_sensor_bind_map(s_sensor_bind_map, MOD_SENSOR_CHANNEL_NUM);

    // 2. 初始化底盘相关模块。
    mod_motor_init();
    mod_sensor_init();
    mod_motor_set_mode(MOD_MOTOR_LEFT, MOTOR_MODE_DRIVE);
    mod_motor_set_mode(MOD_MOTOR_RIGHT, MOTOR_MODE_DRIVE);

    // 3. 初始化外环位置PID。
    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

    // 4. 初始化内环速度PID。
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

    osDelay(3000U);

    // 5. 主循环：固定20ms执行一次。
    for (;;)
    {
        // 5.1 非阻塞查询模式切换信号。
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            // 5.1.1 模式翻转：0 <-> 1。
            control_mode ^= 1U;

            // 5.1.2 切换模式时复位PID状态。
            PID_Pos_Reset(&pos_pid);
            PID_Inc_Reset(&left_speed_pid);
            PID_Inc_Reset(&right_speed_pid);
            PID_Pos_SetTarget(&pos_pid, (float)MOTOR_TARGET_ERROR);

            // 5.1.3 切换瞬间先清零输出。
            mod_motor_set_duty(MOD_MOTOR_LEFT, 0);
            mod_motor_set_duty(MOD_MOTOR_RIGHT, 0);
        }

        // 5.2 刷新编码器速度与累计位置。
        mod_motor_tick();

        // 5.3 模式0：外环位置式 + 内环速度式。
        if (control_mode == TASK_DCC_MODE_BALANCE)
        {
            int64_t left_pos;
            int64_t right_pos;
            float pos_error;
            float outer_output;

            float left_target_speed;
            float right_target_speed;
            float left_feedback_speed;
            float right_feedback_speed;

            float left_duty_f;
            float right_duty_f;
            float vofa_payload[4];

            // 5.3.1 读取左右轮累计位置并计算误差。
            left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
            right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
            pos_error = (float)(left_pos - right_pos);

            // 5.3.2 计算外环输出（内部误差：target - measure）。
            outer_output = PID_Pos_Compute(&pos_pid, -pos_error);
            outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

            // 5.3.3 计算左右轮目标速度。
            left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
            right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

            // 5.3.4 读取左右轮速度反馈。
            left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
            right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

            // 5.3.5 计算内环输出。
            PID_Inc_SetTarget(&left_speed_pid, left_target_speed);
            PID_Inc_SetTarget(&right_speed_pid, right_target_speed);
            left_duty_f = PID_Inc_Compute(&left_speed_pid, left_feedback_speed);
            right_duty_f = PID_Inc_Compute(&right_speed_pid, right_feedback_speed);

            // 5.3.6 下发电机占空比。
            mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
            mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));

            // 5.3.7 仅在 VOFA 已绑定时才发送，避免串口未初始化导致的无效发送
            if (mod_vofa_is_bound(p_vofa_ctx))
            {
                // 5.3.7.1 按你原先约定的 4 通道顺序打包，避免 VOFA 工程重新配通道
                vofa_payload[0] = (float)MOTOR_TARGET_SPEED; // CH0：全局目标速度
                vofa_payload[1] = left_target_speed; // CH1：左轮目标速度
                vofa_payload[2] = right_target_speed; // CH2：右轮目标速度
                vofa_payload[3] = pos_error; // CH3：左右轮位置误差（左-右）

                // 5.3.7.2 通过统一标签发送，便于 VOFA 侧一次配置长期复用
                (void)mod_vofa_send_float_ctx(p_vofa_ctx, "DccCtrl", vofa_payload, 4U);
            }
        }
        else
        {
            // 5.4 模式1：循迹模式预留。
        }

        // 5.5 固定调度周期。
        osDelay(TASK_DCC_PERIOD_MS);
    }
}
