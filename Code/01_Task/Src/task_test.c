#include "task_test.h"

void StartTestTask(void *argument)
{
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx(); // VOFA 默认上下文指针
    int32_t test_count = 0; // 测试计数器
    (void)argument; // 任务参数当前未使用

    //1. 主循环：当前暂时关闭 TestTask 的 VOFA 周期发送
    for (;;)
    {
        //1.1 发送逻辑暂时注释，避免与底盘调参数据混发
        //if (mod_vofa_is_bound(p_vofa_ctx))
        //{
        //    (void)mod_vofa_send_int_ctx(p_vofa_ctx, "TestCnt", &test_count, 1U);
        //    test_count++;
        //}

        (void)p_vofa_ctx;
        (void)test_count;

        //1.2 固定周期调度
        osDelay(TASK_TEST_PERIOD_MS);
    }
}
