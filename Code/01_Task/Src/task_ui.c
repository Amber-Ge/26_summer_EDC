/**
 * @file    task_ui.c
 * @brief   UI task.
 */

#include "task_ui.h"

#include <stdio.h>

#include "cmsis_os.h"
#include "task_init.h"

#include "mod_oled.h"

#include "task_chassis.h"
#include "task_input.h"

#define TASK_UI_PERIOD_MS (100U)

static char *task_ui_get_mode_text(task_chassis_cmd_mode_t mode)
{
    switch (mode)
    {
        case TASK_CHASSIS_CMD_MODE_SPEED_DEBUG:
            return "SPD";
        case TASK_CHASSIS_CMD_MODE_PWM_DEBUG:
            return "PWM";
        case TASK_CHASSIS_CMD_MODE_LINE_TRACK:
            return "TRACK";
        case TASK_CHASSIS_CMD_MODE_IDLE:
        default:
            return "IDLE";
    }
}

void StartUiTask(void *argument)
{
    task_chassis_telemetry_t telemetry;
    char *mode_text;
    char lap_text[8];

    (void)argument;

    task_wait_init_done();

    for (;;)
    {
        if (task_chassis_get_telemetry(&telemetry))
        {
            mode_text = task_ui_get_mode_text(telemetry.mode);
        }
        else
        {
            mode_text = "ERR";
        }

        OLED_Clear();
        OLED_ShowString(0, 0, "MODE", OLED_8X16);
        OLED_ShowString(0, 18, mode_text, OLED_8X16);
        OLED_ShowString(64, 0, "LAP", OLED_8X16);
        if (task_input_is_lap_select_active())
        {
            snprintf(lap_text, sizeof(lap_text), "%u", (unsigned int)task_input_get_selected_laps());
            OLED_ShowString(64, 18, "SEL", OLED_8X16);
            OLED_ShowString(96, 18, lap_text, OLED_8X16);
        }
        else
        {
            OLED_ShowString(64, 18, "--", OLED_8X16);
        }
        OLED_Update();

        osDelay(TASK_UI_PERIOD_MS);
    }
}
