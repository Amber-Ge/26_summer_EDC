#include "task_test.h"
#include "task_init.h"

/**
 * @brief 测试任务入口。
 *
 * @details
 * 当前版本测试发送逻辑处于预留状态，本任务只保留调度框架：
 * 1. 等待 InitTask 放行。
 * 2. 进入固定周期空转，便于后续快速恢复测试代码。
 */
void StartTestTask(void *argument)
{
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx();
    int32_t test_count = 0;

    (void)argument;

    /* 保证系统基础模块初始化完成后再进入测试任务循环。 */
    task_wait_init_done();

    for (;;)
    {
        /* 变量保留用于后续测试扩展，当前避免未使用告警。 */
        (void)p_vofa_ctx;
        (void)test_count;

        /* 固定任务节拍。 */
        osDelay(TASK_TEST_PERIOD_MS);
    }
}
