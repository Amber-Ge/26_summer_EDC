#include "mod_motor.h"


// 硬件配置结构体：将所有的底层硬件资源绑定在一个结构体内，方便数组化管理
typedef struct
{
    GPIO_TypeDef *in1_port;     // 控制方向引脚1的端口
    uint16_t in1_pin;           // 控制方向引脚1的引脚号
    GPIO_TypeDef *in2_port;     // 控制方向引脚2的端口
    uint16_t in2_pin;           // 控制方向引脚2的引脚号

    TIM_HandleTypeDef *pwm_htim; // PWM 定时器句柄
    uint32_t pwm_channel;        // PWM 通道
    bool pwm_invert;             // PWM 逻辑反转标志

    TIM_HandleTypeDef *enc_htim; // 编码器定时器句柄
    uint8_t enc_counter_bits;    // 编码器位宽
    bool enc_invert;             // 编码器方向反转标志（用于修正物理安装导致的极性反转）
} motor_hw_cfg_t;

// 电机状态机结构体：记录电机的当前运行环境和历史数据
typedef struct
{
    mod_motor_mode_e mode;       // 当前运行模式（动力/刹车/滑行）
    int8_t last_sign;            // 上次驱动指令的方向符号（1正转，-1反转，0停止）
    uint8_t zero_cross_pending;  // 过零保护挂起标志：1表示正在等待下一次Tick执行反转指令
    int16_t pending_duty;        // 被挂起的待执行反转占空比指令
    int32_t current_speed;       // 当前速度（脉冲/Tick周期）
    int64_t total_position;      // 累计总里程（脉冲）
    bool hw_ready;               // 硬件底层初始化是否成功的标志
} motor_state_t;

// 1. 静态实例化硬件配置表（数据驱动设计，解耦业务逻辑与硬件绑定）
static const motor_hw_cfg_t s_hw_cfg[MOD_MOTOR_MAX] =
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
        .enc_invert = true, // 右轮因镜像安装，编码器读数需反向
    },
};

// 2. 静态实例化设备状态与底层驱动句柄
static motor_state_t s_motor_state[MOD_MOTOR_MAX]; // 电机运行时状态数组
static drv_pwm_dev_t s_pwm_dev[MOD_MOTOR_MAX]; // PWM 驱动设备对象数组
static drv_encoder_dev_t s_enc_dev[MOD_MOTOR_MAX]; // 编码器驱动设备对象数组

static bool is_valid_motor_id(mod_motor_id_e id)
{
    bool result = false; // 1. 默认无效
    if ((id >= MOD_MOTOR_LEFT) && (id < MOD_MOTOR_MAX))
        result = true;
    return result; // 2. 单出口返回
}

//限制幅度
static int16_t clamp_duty(int16_t duty)
{
    int16_t result = duty; // 1. 默认取传入值

    // 2. 限制在正向最大值以内
    if (duty > (int16_t)MOD_MOTOR_DUTY_MAX)
        result = (int16_t)MOD_MOTOR_DUTY_MAX;
    // 3. 限制在反向最大值以内
    else if (duty < -(int16_t)MOD_MOTOR_DUTY_MAX)
        result = -(int16_t)MOD_MOTOR_DUTY_MAX;

    return result; // 4. 单出口返回限幅后的结果
}

//设置tb6612方向电平
static void set_half_bridge(mod_motor_id_e id, gpio_level_e in1_level, gpio_level_e in2_level)
{
    const motor_hw_cfg_t *p_hw = &s_hw_cfg[id]; // 提取对应电机的硬件配置

    // 1. 调用底层的 GPIO 驱动，设置 TB6612 的两个方向控制引脚
    drv_gpio_write(p_hw->in1_port, p_hw->in1_pin, in1_level);
    drv_gpio_write(p_hw->in2_port, p_hw->in2_pin, in2_level);
}

//设置前进模式
static void apply_drive(mod_motor_id_e id, uint16_t duty, int8_t sign)
{
    // 1. 确保硬件底层初始化成功才进行操作
    if (s_motor_state[id].hw_ready)
    {
        // 2. 根据方向符号决定 IN1 和 IN2 的电平组合
        if (sign > 0)
            set_half_bridge(id, GPIO_LEVEL_HIGH, GPIO_LEVEL_LOW);
        else if (sign < 0)
            set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_HIGH);
        else
        {
            // 3. 符号为 0 时进入待机滑行状态，并强制将占空比置 0
            set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);
            duty = 0U;
        }

        // 4. 将计算好的正值占空比下发给 PWM 底层驱动
        drv_pwm_set_duty(&s_pwm_dev[id], duty);
    }
}

//设置刹车模式
static void apply_brake(mod_motor_id_e id)
{
    // 1. 设置 H 桥的两根引脚全为高电平，触发 TB6612 的短路刹车模式
    set_half_bridge(id, GPIO_LEVEL_HIGH, GPIO_LEVEL_HIGH);
    
    // 2. 为了增强刹车力度，将 PWM 占空比拉到最大
    if (s_motor_state[id].hw_ready)
        drv_pwm_set_duty(&s_pwm_dev[id], drv_pwm_get_duty_max(&s_pwm_dev[id]));
}

//设置滑行模式
static void apply_coast(mod_motor_id_e id)
{
    // 1. 设置 H 桥引脚全低，断开电机供电，使其进入自由滑行状态
    set_half_bridge(id, GPIO_LEVEL_LOW, GPIO_LEVEL_LOW);
    
    // 2. 占空比清零
    if (s_motor_state[id].hw_ready)
        drv_pwm_set_duty(&s_pwm_dev[id], 0U);
}


static void execute_drive_cmd(mod_motor_id_e id, int16_t duty_cmd)
{
    motor_state_t *p_state = &s_motor_state[id]; // 获取状态指针
    int16_t duty = clamp_duty(duty_cmd);         // 对输入指令进行安全限幅
    int8_t sign = 0;                             // 提取方向符号
    uint16_t abs_duty = 0U;                      // 提取绝对占空比

    // 1. 解析传入指令，分离出方向和绝对大小
    if (duty > 0)
    {
        sign = 1;
        abs_duty = (uint16_t)duty;
    }
    else if (duty < 0)
    {
        sign = -1;
        abs_duty = (uint16_t)(-duty);
    }

    // 2. 调用驱动生效函数
    apply_drive(id, abs_duty, sign);
    
    // 3. 记录本次真实下发给硬件的方向符号，供下次比较进行过零保护
    p_state->last_sign = sign;
}

void mod_motor_init(void)
{
    // 1. 遍历所有注册的电机
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        const motor_hw_cfg_t *p_hw = &s_hw_cfg[i];
        motor_state_t *p_state = &s_motor_state[i];
        bool pwm_ok = false;
        bool enc_ok = false;

        // 2. 状态机参数归零初始化
        p_state->mode = MOTOR_MODE_COAST;
        p_state->last_sign = 0;
        p_state->zero_cross_pending = 0U;
        p_state->pending_duty = 0;
        p_state->current_speed = 0;
        p_state->total_position = 0;

        // 3. 依次初始化并启动 PWM 底层和编码器底层
        pwm_ok = drv_pwm_device_init(&s_pwm_dev[i], p_hw->pwm_htim, p_hw->pwm_channel, MOD_MOTOR_DUTY_MAX, p_hw->pwm_invert);
        if (pwm_ok)
            pwm_ok = drv_pwm_start(&s_pwm_dev[i]);

        enc_ok = drv_encoder_device_init(&s_enc_dev[i], p_hw->enc_htim, p_hw->enc_counter_bits, p_hw->enc_invert);
        if (enc_ok)
            enc_ok = drv_encoder_start(&s_enc_dev[i]);

        // 4. 只有当 PWM 和编码器都启动成功时，该电机的硬件状态才算就绪
        p_state->hw_ready = pwm_ok && enc_ok;
        
        // 5. 开机默认进入自由滑行状态策安全
        apply_coast((mod_motor_id_e)i);
    }
}

void mod_motor_set_mode(mod_motor_id_e id, mod_motor_mode_e mode)
{
    // 1. 拦截非法 ID 请求
    if (is_valid_motor_id(id))
    {
        // 2. 覆盖当前模式，并强制清除任何未执行完毕的过零挂起指令
        s_motor_state[id].mode = mode;
        s_motor_state[id].zero_cross_pending = 0U;

        // 3. 根据目标模式直接干预硬件底层
        if (mode == MOTOR_MODE_BRAKE)
            apply_brake(id);
        else if (mode == MOTOR_MODE_COAST)
            apply_coast(id);
        // MOTOR_MODE_DRIVE 模式由 mod_motor_set_duty 函数接管驱动逻辑，这里不做硬件操作
    }
}

void mod_motor_set_duty(mod_motor_id_e id, int16_t duty)
{
    motor_state_t *p_state;
    int16_t duty_clamped;
    int8_t current_sign = 0;

    // 1. 首先确保 ID 合法
    if (is_valid_motor_id(id))
    {
        p_state = &s_motor_state[id];
        
        // 2. 只有硬件就绪且处于动力驱动模式时，才响应占空比指令
        if (p_state->hw_ready && (p_state->mode == MOTOR_MODE_DRIVE))
        {
            // 3. 对目标指令进行限幅，并提取目标符号
            duty_clamped = clamp_duty(duty);
            if (duty_clamped > 0)
                current_sign = 1;
            else if (duty_clamped < 0)
                current_sign = -1;

            // 4. 过零保护核心判断：如果上次正在转动，且本次目标也要转动，但方向相反
            if ((p_state->last_sign != 0) && (current_sign != 0) && (p_state->last_sign != current_sign))
            {
                // 5. 不直接执行反转！先让硬件进入滑行状态泄放能量
                apply_coast(id);
                // 6. 将新的反转指令挂起，交由 tick 节拍器稍后执行
                p_state->zero_cross_pending = 1U;
                p_state->pending_duty = duty_clamped;
                p_state->last_sign = current_sign;
            }
            else
            {
                // 7. 同向驱动或者由静止启动，直接执行指令
                if (p_state->zero_cross_pending == 0U)
                    execute_drive_cmd(id, duty_clamped);
                else
                    // 8. 处于正在保护等待期间又收到了同向的新指令，仅更新挂起的缓存值即可
                    p_state->pending_duty = duty_clamped;
            }
        }
    }
}

void mod_motor_tick(void)
{
    // 1. 遍历并更新所有电机
    for (uint8_t i = 0U; i < MOD_MOTOR_MAX; i++)
    {
        motor_state_t *p_state = &s_motor_state[i];

        // 2. 检查硬件状态，若未就绪，强制将数据清零以保障上层逻辑安全
        if (p_state->hw_ready)
        {
            // 3. 从底层读取本次 Tick 周期的增量脉冲数，存为当前速度
            p_state->current_speed = drv_encoder_get_delta(&s_enc_dev[i]);
            
            // 4. 将当前周期的增量积分，更新至总路程位置
            p_state->total_position += p_state->current_speed;

            // 5. 检查是否存在被过零保护机制挂起的延迟指令
            if (p_state->zero_cross_pending != 0U)
            {
                // 6. 经过一个 Tick 周期（假设 10ms~20ms）的滑行，能量泄放完毕，清除挂起标志
                p_state->zero_cross_pending = 0U;
                
                // 7. 若当前仍处于驱动模式，则真正下发被暂存的反转指令
                if (p_state->mode == MOTOR_MODE_DRIVE)
                    execute_drive_cmd((mod_motor_id_e)i, p_state->pending_duty);
            }
        }
        else
            // 硬件不可用时的安全退路
            p_state->current_speed = 0;
    }
}

int32_t mod_motor_get_speed(mod_motor_id_e id)
{
    int32_t result = 0; // 1. 默认速度为 0

    // 2. 若 ID 合法，则返回状态机中记录的当前速度
    if (is_valid_motor_id(id))
        result = s_motor_state[id].current_speed;

    // 3. 单出口返回
    return result; 
}

int64_t mod_motor_get_position(mod_motor_id_e id)
{
    int64_t result = 0; // 1. 默认位置为 0

    // 2. 若 ID 合法，则返回状态机中积分得到的累计总里程
    if (is_valid_motor_id(id))
        result = s_motor_state[id].total_position;

    // 3. 单出口返回
    return result;
}
