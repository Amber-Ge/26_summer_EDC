#include "task_pc.h"
#include "mod_vofa.h"
#include <string.h>

void StartPcTask(void *argument)
{
    mod_vofa_ctx_t *p_vofa_ctx = mod_vofa_get_default_ctx(); // VOFA 默认上下文指针
    mod_vofa_bind_t vofa_bind; // VOFA 绑定参数
    static bool s_vofa_can_send = false; // 周期发送使能标志（当前仅用于等待节拍）
    int32_t count_val = 0; // 周期计数值（发送注释后仅保留调试变量）
    (void)argument; // 任务参数当前未使用

    //1. 组装 VOFA 绑定信息（串口/信号量/互斥锁）
    memset(&vofa_bind, 0, sizeof(vofa_bind));
    vofa_bind.huart = TASK_PC_HUART;
    vofa_bind.sem_list[0] = Sem_PcHandle;
    vofa_bind.sem_count = 1U;
    vofa_bind.tx_mutex = PcMutexHandle;

    //2. 初始化或重新绑定 VOFA 上下文
    if (!p_vofa_ctx->inited)
    {
        (void)mod_vofa_ctx_init(p_vofa_ctx, &vofa_bind);
    }
    else
    {
        (void)mod_vofa_bind(p_vofa_ctx, &vofa_bind);
    }

    //3. 主循环：处理串口命令
    for (;;)
    {
        uint32_t wait_time = s_vofa_can_send ? TASK_PC_PERIOD_MS : osWaitForever; // 启动后周期轮询，停止后阻塞等待

        //3.1 等待命令信号量，若收到命令则优先处理
        if (osSemaphoreAcquire(Sem_PcHandle, wait_time) == osOK)
        {
            vofa_cmd_id_t cmd = mod_vofa_get_command_ctx(p_vofa_ctx); // 当前解析出的命令 ID

            if (cmd == VOFA_CMD_START)
            {
                float ack = (float)cmd; // 启动应答值
                s_vofa_can_send = true;

                // 暂时注释：避免与 DCC 数据混发
                //(void)mod_vofa_send_float_ctx(p_vofa_ctx, "StartAck", &ack, 1U);
                (void)ack;
            }
            else if (cmd == VOFA_CMD_STOP)
            {
                float ack = (float)cmd; // 停止应答值
                s_vofa_can_send = false;
                count_val = 0;

                // 暂时注释：避免与 DCC 数据混发
                //(void)mod_vofa_send_float_ctx(p_vofa_ctx, "StopAck", &ack, 1U);
                (void)ack;
            }

            continue;
        }

        //3.2 原周期发送功能暂时注释，当前仅由 task_dcc 发送 VOFA 数据
        if (s_vofa_can_send)
        {
            count_val++;
            //(void)mod_vofa_send_int_ctx(p_vofa_ctx, "Count", &count_val, 1U);
            (void)count_val;
        }
    }
}
