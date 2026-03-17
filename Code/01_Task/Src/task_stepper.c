#include "task_stepper.h"
#include "mod_vofa.h"
#include <string.h>

#if (TASK_STEPPER_ENABLE_CH1 == 0U) && (TASK_STEPPER_ENABLE_CH2 == 0U)
#error "task_stepper 至少需要启用一个通道"
#endif

#define TASK_STEPPER_CHANNEL_COUNT ((uint8_t)(TASK_STEPPER_ENABLE_CH1 + TASK_STEPPER_ENABLE_CH2))

typedef struct
{
    UART_HandleTypeDef *huart;
    osSemaphoreId_t sem_id;
    uint8_t logic_id;
    uint8_t driver_addr;
    uint16_t max_speed_rpm;
} task_stepper_channel_cfg_t;

typedef struct
{
    mod_stepper_ctx_t mod_ctx;
    mod_stepper_bind_t bind;
    task_stepper_state_t state;
    uint32_t last_oflag_poll_tick;
} task_stepper_channel_t;

static const task_stepper_channel_cfg_t s_channel_cfg[TASK_STEPPER_CHANNEL_COUNT] =
{
#if (TASK_STEPPER_ENABLE_CH1 == 1U)
    {
        .huart = TASK_STEPPER_CH1_HUART,
        .sem_id = NULL,
        .logic_id = TASK_STEPPER_CH1_LOGIC_ID,
        .driver_addr = TASK_STEPPER_CH1_DRIVER_ADDR,
        .max_speed_rpm = TASK_STEPPER_CH1_MAX_SPEED_RPM
    },
#endif
#if (TASK_STEPPER_ENABLE_CH2 == 1U)
    {
        .huart = TASK_STEPPER_CH2_HUART,
        .sem_id = NULL,
        .logic_id = TASK_STEPPER_CH2_LOGIC_ID,
        .driver_addr = TASK_STEPPER_CH2_DRIVER_ADDR,
        .max_speed_rpm = TASK_STEPPER_CH2_MAX_SPEED_RPM
    },
#endif
};

static task_stepper_channel_t s_channels[TASK_STEPPER_CHANNEL_COUNT];

static task_stepper_channel_t *_find_channel(uint8_t logic_id)
{
    for (uint16_t i = 0U; i < TASK_STEPPER_CHANNEL_COUNT; i++)
    {
        if (s_channels[i].state.configured && (s_channels[i].state.logic_id == logic_id))
        {
            return &s_channels[i];
        }
    }

    return NULL;
}

static void _update_home_state_from_oflag(task_stepper_channel_t *ch, uint8_t oflag)
{
    if (ch == NULL)
    {
        return;
    }

    if (oflag == TASK_STEPPER_OFLAG_SUCCESS_VALUE)
    {
        ch->state.home_state = TASK_STEPPER_HOME_SUCCESS;
    }
    else if (oflag == TASK_STEPPER_OFLAG_FAIL_VALUE)
    {
        ch->state.home_state = TASK_STEPPER_HOME_FAIL;
    }
}

static void _drain_ack(task_stepper_channel_t *ch)
{
    uint8_t cmd;
    uint8_t status;

    if ((ch == NULL) || !ch->state.bound)
    {
        return;
    }

    while (mod_stepper_get_last_ack(&ch->mod_ctx, &cmd, &status))
    {
        ch->state.last_ack_cmd = cmd;
        ch->state.last_ack_status = status;
    }
}

static void _drain_frame(task_stepper_channel_t *ch)
{
    uint8_t frame[MOD_STEPPER_RX_FRAME_BUF_SIZE];
    uint16_t frame_len;

    if ((ch == NULL) || !ch->state.bound)
    {
        return;
    }

    for (;;)
    {
        frame_len = (uint16_t)sizeof(frame);
        if (!mod_stepper_get_last_frame(&ch->mod_ctx, frame, &frame_len))
        {
            break;
        }

        ch->state.rx_frame_count++;

        if ((frame_len >= 4U) && (frame[frame_len - 1U] == MOD_STEPPER_FRAME_TAIL))
        {
            if (frame[1] == 0x3BU)
            {
                ch->state.oflag_raw = frame[frame_len - 2U];
                ch->state.oflag_valid = true;
                _update_home_state_from_oflag(ch, ch->state.oflag_raw);
            }
        }
    }
}

static void _init_all_channels(void)
{
    mod_stepper_bind_t bind;

    memset(s_channels, 0, sizeof(s_channels));

    for (uint16_t i = 0U; i < TASK_STEPPER_CHANNEL_COUNT; i++)
    {
        memset(&bind, 0, sizeof(bind));
        bind.huart = s_channel_cfg[i].huart;
        bind.sem_id = s_channel_cfg[i].sem_id;
        bind.motor_id = s_channel_cfg[i].logic_id;
        bind.driver_addr = s_channel_cfg[i].driver_addr;

        s_channels[i].bind = bind;
        s_channels[i].state.configured = true;
        s_channels[i].state.logic_id = bind.motor_id;
        s_channels[i].state.driver_addr = bind.driver_addr;
        s_channels[i].state.max_speed_rpm = s_channel_cfg[i].max_speed_rpm;
        s_channels[i].state.home_state = TASK_STEPPER_HOME_IDLE;
        s_channels[i].state.bound = mod_stepper_init(&s_channels[i].mod_ctx, &bind);
    }
}

static uint16_t _limit_speed_by_channel(const task_stepper_channel_t *ch, uint16_t req_speed_rpm)
{
    uint16_t limit_speed;

    if (ch == NULL)
    {
        return req_speed_rpm;
    }

    limit_speed = ch->state.max_speed_rpm;
    if ((limit_speed == 0U) || (limit_speed > MOD_STEPPER_VEL_MAX_RPM))
    {
        limit_speed = MOD_STEPPER_VEL_MAX_RPM;
    }

    if (req_speed_rpm > limit_speed)
    {
        return limit_speed;
    }

    return req_speed_rpm;
}

static bool _send_position_cmd(task_stepper_channel_t *ch)
{
    uint16_t cmd_speed_rpm;

    if ((ch == NULL) || !ch->state.bound)
    {
        return false;
    }

    cmd_speed_rpm = _limit_speed_by_channel(ch, TASK_STEPPER_POS_SPEED_RPM);
    if (cmd_speed_rpm == 0U)
    {
        return false;
    }

    if (mod_stepper_position(&ch->mod_ctx,
                             ch->bind.motor_id,
                             TASK_STEPPER_POS_DIR,
                             cmd_speed_rpm,
                             TASK_STEPPER_POS_ACC,
                             TASK_STEPPER_POS_PULSE,
                             TASK_STEPPER_POS_ABSOLUTE,
                             TASK_STEPPER_POS_SYNC_FLAG))
    {
        ch->state.pos_cmd_speed_rpm = cmd_speed_rpm;
        ch->state.tx_cmd_count++;
        return true;
    }

    return false;
}

static void _poll_oflag_if_needed(task_stepper_channel_t *ch)
{
    uint32_t now;

    if ((ch == NULL) || !ch->state.bound)
    {
        return;
    }

    now = HAL_GetTick();
    if ((now - ch->last_oflag_poll_tick) < TASK_STEPPER_OFLAG_POLL_MS)
    {
        return;
    }

    ch->last_oflag_poll_tick = now;
    if (mod_stepper_read_sys_param(&ch->mod_ctx, ch->bind.motor_id, MOD_STEPPER_SYS_OFLAG))
    {
        ch->state.tx_cmd_count++;
    }
}

static bool _home_once(task_stepper_channel_t *ch)
{
    uint32_t start_tick;

    if ((ch == NULL) || !ch->state.bound)
    {
        return false;
    }

    if (!mod_stepper_origin_trigger(&ch->mod_ctx, ch->bind.motor_id, MOD_STEPPER_ORIGIN_MODE_NEAREST, false))
    {
        return false;
    }
    ch->state.tx_cmd_count++;

    ch->state.home_state = TASK_STEPPER_HOME_RUNNING;
    ch->state.home_start_tick = HAL_GetTick();
    start_tick = ch->state.home_start_tick;

    while ((HAL_GetTick() - start_tick) < TASK_STEPPER_HOME_TIMEOUT_MS)
    {
        mod_stepper_process(NULL);
        _drain_ack(ch);
        _drain_frame(ch);
        _poll_oflag_if_needed(ch);

        if (ch->state.home_state == TASK_STEPPER_HOME_SUCCESS)
        {
            return true;
        }

        if (ch->state.home_state == TASK_STEPPER_HOME_FAIL)
        {
            return false;
        }

        osDelay(TASK_STEPPER_PERIOD_MS);
    }

    ch->state.home_state = TASK_STEPPER_HOME_TIMEOUT;
    return false;
}

static void _send_home_result_once(task_stepper_channel_t *ch)
{
    mod_vofa_ctx_t *vofa_ctx;
    uint32_t start_tick;
    const char *msg;

    if (ch == NULL)
    {
        return;
    }

    vofa_ctx = mod_vofa_get_default_ctx();
    start_tick = HAL_GetTick();
    while (!mod_vofa_is_bound(vofa_ctx))
    {
        if ((HAL_GetTick() - start_tick) >= TASK_STEPPER_VOFA_WAIT_MS)
        {
            return;
        }
        osDelay(20U);
    }

    if (ch->state.home_state == TASK_STEPPER_HOME_SUCCESS)
    {
        msg = "success";
    }
    else
    {
        msg = "fail";
    }

    (void)mod_vofa_send_string_ctx(vofa_ctx, msg);
}

void StartStepperTask(void *argument)
{
    task_stepper_channel_t *ch;

    (void)argument;

#if (TASK_STEPPER_STARTUP_ENABLE == 0U)
    /* 关闭开关时任务自挂起，不占用调度 */
    (void)osThreadSuspend(osThreadGetId());
    for (;;)
    {
        osDelay(osWaitForever);
    }
#endif

    osDelay(TASK_STEPPER_PREPARE_MS);
    _init_all_channels();

    ch = _find_channel(TASK_STEPPER_CH1_LOGIC_ID);
    if ((ch == NULL) || !ch->state.bound)
    {
        for (;;)
        {
            osDelay(1000U);
        }
    }

    if (mod_stepper_enable(&ch->mod_ctx, ch->bind.motor_id, true, false))
    {
        ch->state.tx_cmd_count++;
    }

    /* 上电先归零，归零完成后再进入位置模式循环 */
    (void)_home_once(ch);
    _send_home_result_once(ch);
    osDelay(TASK_STEPPER_AFTER_HOME_DELAY_MS);

    for (;;)
    {
        /* 推进底层收发队列并同步状态 */
        mod_stepper_process(NULL);
        _drain_ack(ch);
        _drain_frame(ch);
        /* 压力测试模式：每20ms固定下发一次位置模式命令 */
        (void)_send_position_cmd(ch);

        osDelay(TASK_STEPPER_PERIOD_MS);
    }
}

const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id)
{
    task_stepper_channel_t *ch = _find_channel(logic_id);

    if (ch == NULL)
    {
        return NULL;
    }

    return &ch->state;
}
