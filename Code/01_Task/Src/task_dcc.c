#include "task_dcc.h"
#include "task_init.h"
#include "mod_vofa.h"

/**
 * @brief 灰度传感器连续触发急停阈值
 *
 * @details
 * 在 mode=1 直线控制模式中，每个 DCC 周期读取一次灰度原始位图：
 * 1. 任意一路触发都算“本周期触发一次”。
 * 2. 连续触发次数达到该阈值后立即急停。
 */
#define TASK_DCC_GRAY_STOP_COUNT (3U)

/**
 * @brief DCC 当前模式（只允许 DCC 任务写入，其他任务只读）
 */
static volatile uint8_t s_dcc_mode = TASK_DCC_MODE_IDLE;

/**
 * @brief DCC 当前 ready 标志（只允许 DCC 任务写入，其他任务只读）
 *
 * @details
 * 1. ready=0：未就绪，DCC 不输出控制。
 * 2. ready=1：就绪，DCC 按 mode 进入对应逻辑。
 */
static volatile uint8_t s_dcc_ready = 0U;

/**
 * @brief 浮点限幅函数
 * @param value 输入值
 * @param min 最小允许值
 * @param max 最大允许值
 * @return float 限幅后的结果
 */
static float clamp_float(float value, float min, float max)
{
    float result = value; // 结果变量：先等于输入，再进行上下限裁剪

    if (result < min)
    {
        result = min; // 小于下限时钳到下限
    }
    if (result > max)
    {
        result = max; // 大于上限时钳到上限
    }

    return result; // 返回限幅后的结果
}

/**
 * @brief 把浮点占空比转换为 int16 命令值
 * @param duty_f 浮点占空比
 * @return int16_t 可直接传给 mod_motor_set_duty 的命令值
 */
static int16_t convert_to_duty_cmd(float duty_f)
{
    float duty_limited; // 限幅后的浮点占空比

    duty_limited = clamp_float(duty_f, -(float)MOD_MOTOR_DUTY_MAX, (float)MOD_MOTOR_DUTY_MAX);
    if (duty_limited >= 0.0f)
    {
        duty_limited += 0.5f; // 正数四舍五入
    }
    else
    {
        duty_limited -= 0.5f; // 负数四舍五入
    }

    return (int16_t)duty_limited; // 转换成电机接口命令类型
}

/**
 * @brief 双电机立即停转
 *
 * @details
 * 用于模式切换、ready切换、急停等场景，确保先进入安全输出状态。
 */
static void dcc_stop_motor_now(void)
{
    mod_motor_set_duty(MOD_MOTOR_LEFT, 0);  // 左电机占空比置0
    mod_motor_set_duty(MOD_MOTOR_RIGHT, 0); // 右电机占空比置0
}

/**
 * @brief 重置 DCC 使用的全部 PID 状态
 * @param pos_pid 位置环 PID 指针
 * @param left_speed_pid 左轮速度环 PID 指针
 * @param right_speed_pid 右轮速度环 PID 指针
 */
static void dcc_reset_pid(pid_pos_t *pos_pid, pid_inc_t *left_speed_pid, pid_inc_t *right_speed_pid)
{
    if ((pos_pid == NULL) || (left_speed_pid == NULL) || (right_speed_pid == NULL))
    {
        return; // 防御式检查：空指针直接返回，避免非法访问
    }

    PID_Pos_Reset(pos_pid);                     // 清空位置环内部状态
    PID_Inc_Reset(left_speed_pid);              // 清空左轮速度环内部状态
    PID_Inc_Reset(right_speed_pid);             // 清空右轮速度环内部状态
    PID_Pos_SetTarget(pos_pid, MOTOR_TARGET_ERROR); // 恢复位置环目标值
}

/**
 * @brief 获取当前 DCC 模式
 * @return uint8_t 当前模式值（0/1/2）
 */
uint8_t task_dcc_get_mode(void)
{
    return s_dcc_mode; // 返回模式快照
}

/**
 * @brief 获取当前 DCC ready 标志
 * @return uint8_t 当前 ready 值（0/1）
 */
uint8_t task_dcc_get_ready(void)
{
    return s_dcc_ready; // 返回 ready 快照
}

/**
 * @brief 底盘控制任务入口函数
 * @param argument 任务参数指针（当前未使用）
 */
void StartDccTask(void *argument)
{
    pid_pos_t pos_pid;                   // 外环：左右位置差PID
    pid_inc_t left_speed_pid;            // 内环：左轮速度PID
    pid_inc_t right_speed_pid;           // 内环：右轮速度PID
    mod_vofa_ctx_t *p_vofa_ctx;          // VOFA上下文指针
    uint8_t gray_trigger_streak = 0U;    // 灰度连续触发计数器

    (void)argument; // 当前实现不使用任务参数

    // 启动门控：等待 InitTask 完成全局初始化
    task_wait_init_done();

    // 读取 VOFA 默认上下文
    p_vofa_ctx = mod_vofa_get_default_ctx();

    // 初始化位置环PID
    PID_Pos_Init(&pos_pid,
                 MOTOR_POS_KP,
                 MOTOR_POS_KI,
                 MOTOR_POS_KD,
                 MOTOR_POS_OUTPUT_MAX,
                 MOTOR_POS_INTEGRAL_MAX);
    PID_Pos_SetTarget(&pos_pid, MOTOR_TARGET_ERROR);

    // 初始化左轮速度环PID
    PID_Inc_Init(&left_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    // 初始化右轮速度环PID
    PID_Inc_Init(&right_speed_pid,
                 MOTOR_SPEED_KP,
                 MOTOR_SPEED_KI,
                 MOTOR_SPEED_KD,
                 (float)MOD_MOTOR_DUTY_MAX);

    // 上电后延时，留给底层驱动和机械系统稳定时间
    osDelay(3000U);

    for (;;)
    {
        // KEY3单击事件：只有在 ready=0 时才允许切 mode
        if (osSemaphoreAcquire(Sem_TaskChangeHandle, 0U) == osOK)
        {
            if (s_dcc_ready == 0U)
            {
                // ready=0：允许 mode 循环切换
                s_dcc_mode = (uint8_t)((s_dcc_mode + 1U) % TASK_DCC_MODE_TOTAL);

                // 切模式后清空状态，避免旧控制状态残留影响新模式
                gray_trigger_streak = 0U;
                dcc_stop_motor_now();
                dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
            }
            else
            {
                // ready=1：按你的要求，禁止 mode 修改，直接忽略单击事件
            }
        }

        // KEY3双击事件：ready 取反（0/1切换）
        if (osSemaphoreAcquire(Sem_ReadyToggleHandle, 0U) == osOK)
        {
            s_dcc_ready ^= 1U; // ready 状态翻转

            // ready切换后统一进入安全状态并清状态
            gray_trigger_streak = 0U;
            dcc_stop_motor_now();
            dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
        }

        // 统一刷新编码器采样（更新速度与位置缓存）
        mod_motor_tick();

        // ready=0：DCC不执行控制，保持停机
        if (s_dcc_ready == 0U)
        {
            dcc_stop_motor_now();
            osDelay(TASK_DCC_PERIOD_MS);
            continue;
        }

        // ready=1 时按 mode 执行业务
        if (s_dcc_mode == TASK_DCC_MODE_IDLE)
        {
            // mode=0：空闲模式，仅保持停机
            gray_trigger_streak = 0U;
            dcc_stop_motor_now();
        }
        else if (s_dcc_mode == TASK_DCC_MODE_STRAIGHT)
        {
            int64_t left_pos;            // 左轮累计位置
            int64_t right_pos;           // 右轮累计位置
            float pos_error;             // 左右位置差（左-右）
            float outer_output;          // 外环输出修正量
            float left_target_speed;     // 左轮目标速度
            float right_target_speed;    // 右轮目标速度
            float left_feedback_speed;   // 左轮反馈速度
            float right_feedback_speed;  // 右轮反馈速度
            float left_duty_f;           // 左轮占空比浮点值
            float right_duty_f;          // 右轮占空比浮点值
            float vofa_payload[4];       // VOFA发送缓存
            uint16_t sensor_raw;         // 灰度原始位图

            // 灰度采样：任意一路触发都算本周期触发
            sensor_raw = mod_sensor_get_raw_data();
            if (sensor_raw != 0U)
            {
                if (gray_trigger_streak < 255U)
                {
                    gray_trigger_streak++; // 连续触发计数加1（带饱和保护）
                }
            }
            else
            {
                gray_trigger_streak = 0U; // 本周期未触发则连续计数清零
            }

            // 连续触发达到阈值：急停并退回 mode=0
            if (gray_trigger_streak >= TASK_DCC_GRAY_STOP_COUNT)
            {
                dcc_stop_motor_now();
                dcc_reset_pid(&pos_pid, &left_speed_pid, &right_speed_pid);
                s_dcc_mode = TASK_DCC_MODE_IDLE;
                gray_trigger_streak = 0U;
                osDelay(TASK_DCC_PERIOD_MS);
                continue;
            }

            // 原有直线控制逻辑：左右位置差 -> 外环修正
            left_pos = mod_motor_get_position(MOD_MOTOR_LEFT);
            right_pos = mod_motor_get_position(MOD_MOTOR_RIGHT);
            pos_error = (float)(left_pos - right_pos);

            // 外环位置PID计算并限幅
            outer_output = PID_Pos_Compute(&pos_pid, -pos_error);
            outer_output = clamp_float(outer_output, -MOTOR_POS_OUTPUT_MAX, MOTOR_POS_OUTPUT_MAX);

            // 将外环输出拆分到左右轮目标速度
            left_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f - outer_output);
            right_target_speed = (float)MOTOR_TARGET_SPEED * (1.0f + outer_output);

            // 读取左右轮速度反馈
            left_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_LEFT);
            right_feedback_speed = (float)mod_motor_get_speed(MOD_MOTOR_RIGHT);

            // 内环速度PID计算占空比
            PID_Inc_SetTarget(&left_speed_pid, left_target_speed);
            PID_Inc_SetTarget(&right_speed_pid, right_target_speed);
            left_duty_f = PID_Inc_Compute(&left_speed_pid, left_feedback_speed);
            right_duty_f = PID_Inc_Compute(&right_speed_pid, right_feedback_speed);

            // 下发占空比到电机
            mod_motor_set_duty(MOD_MOTOR_LEFT, convert_to_duty_cmd(left_duty_f));
            mod_motor_set_duty(MOD_MOTOR_RIGHT, convert_to_duty_cmd(right_duty_f));

            // 若 VOFA 已绑定，则发送调试数据
            if (mod_vofa_is_bound(p_vofa_ctx))
            {
                vofa_payload[0] = (float)MOTOR_TARGET_SPEED; // CH0：全局目标速度
                vofa_payload[1] = left_target_speed;         // CH1：左轮目标速度
                vofa_payload[2] = right_target_speed;        // CH2：右轮目标速度
                vofa_payload[3] = pos_error;                 // CH3：左右位置差
                (void)mod_vofa_send_float_ctx(p_vofa_ctx, "DccCtrl", vofa_payload, 4U);
            }
        }
        else
        {
            // mode=2：黑线循迹预留（当前不执行控制）
            // TODO: 在此处补充黑线循迹控制算法
            gray_trigger_streak = 0U;
            dcc_stop_motor_now();
        }

        // 固定20ms任务周期
        osDelay(TASK_DCC_PERIOD_MS);
    }
}
