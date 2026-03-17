#ifndef FINAL_GRADUATE_WORK_TASK_STEPPER_H
#define FINAL_GRADUATE_WORK_TASK_STEPPER_H

#include "cmsis_os.h"
#include "mod_stepper.h"
#include "usart.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================= 任务测试开关 ========================= */
#define TASK_STEPPER_STARTUP_ENABLE            (1U)

/* ========================= 调度与流程参数 ========================= */
#define TASK_STEPPER_PERIOD_MS                 (20U)
#define TASK_STEPPER_PREPARE_MS                (3000U)
#define TASK_STEPPER_OFLAG_POLL_MS             (200U)
#define TASK_STEPPER_HOME_TIMEOUT_MS           (15000U)
#define TASK_STEPPER_AFTER_HOME_DELAY_MS       (0U)
#define TASK_STEPPER_VOFA_WAIT_MS              (3000U)

/* ========================= OFLAG 解析参数 ========================= */
#define TASK_STEPPER_OFLAG_SUCCESS_VALUE       (1U)
#define TASK_STEPPER_OFLAG_FAIL_VALUE          (2U)

/* ========================= 通道配置 ========================= */
#define TASK_STEPPER_ENABLE_CH1                (1U)
#define TASK_STEPPER_CH1_HUART                 (&huart5)
#define TASK_STEPPER_CH1_LOGIC_ID              (1U)
#define TASK_STEPPER_CH1_DRIVER_ADDR           (1U)
#define TASK_STEPPER_CH1_MAX_SPEED_RPM         (100U)

#define TASK_STEPPER_ENABLE_CH2                (0U)
#define TASK_STEPPER_CH2_HUART                 (&huart2)
#define TASK_STEPPER_CH2_LOGIC_ID              (2U)
#define TASK_STEPPER_CH2_DRIVER_ADDR           (2U)
#define TASK_STEPPER_CH2_MAX_SPEED_RPM         (100U)

/* ========================= 位置模式测试参数 ========================= */
#define TASK_STEPPER_POS_DIR                   (MOD_STEPPER_DIR_CW)
#define TASK_STEPPER_POS_SPEED_RPM             (100U)   /* 位置模式速度：50RPM */
#define TASK_STEPPER_POS_ACC                   (0U)
#define TASK_STEPPER_POS_PULSE                 (200U)
#define TASK_STEPPER_POS_ABSOLUTE              (false)
#define TASK_STEPPER_POS_SYNC_FLAG             (false)
#define TASK_STEPPER_POS_PULSE_PER_REV         (3200U)
#define TASK_STEPPER_POS_FINISH_MARGIN_MS      (40U)
#define TASK_STEPPER_POS_TIMEOUT_MS            (3000U)

typedef enum
{
    TASK_STEPPER_HOME_IDLE = 0U,
    TASK_STEPPER_HOME_RUNNING = 1U,
    TASK_STEPPER_HOME_SUCCESS = 2U,
    TASK_STEPPER_HOME_FAIL = 3U,
    TASK_STEPPER_HOME_TIMEOUT = 4U
} task_stepper_home_state_e;

typedef struct
{
    bool configured;
    bool bound;

    uint8_t logic_id;
    uint8_t driver_addr;

    uint8_t last_ack_cmd;
    uint8_t last_ack_status;

    uint8_t oflag_raw;
    bool oflag_valid;

    task_stepper_home_state_e home_state;

    uint32_t home_start_tick;
    uint32_t pos_start_tick;
    uint32_t pos_expect_done_ms;
    uint16_t max_speed_rpm;
    uint16_t pos_cmd_speed_rpm;

    int16_t vel_feedback_rpm;
    bool vel_feedback_valid;

    bool pos_running;
    bool pos_seen_nonzero;
    uint8_t pos_zero_confirm;

    uint32_t tx_cmd_count;
    uint32_t rx_frame_count;
} task_stepper_state_t;

void StartStepperTask(void *argument);

const task_stepper_state_t *task_stepper_get_state(uint8_t logic_id);

#endif
