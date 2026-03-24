# 分层文档总索引

## 文档元信息

| 字段 | 内容 |
| --- | --- |
| 文档名称 | Layer Document Index |
| 作者 | Codex（基于工程现状整理） |
| 版本 | v1.0.0 |
| 最后更新 | 2026-03-24 |
| 适用工程 | `FInal_graduate_work` |

---

## 1. 推荐阅读顺序

1. 先读 Core 启动层（理解系统如何启动）。
2. 再读 Task 层（理解业务时序和资源编排）。
3. 再读 Module 层（理解功能抽象与解耦边界）。
4. 再读 Driver 层（理解硬件访问接口契约）。
5. 最后读 Common 层（理解算法与工具库）。

---

## 2. 文档清单

1. Core 启动层说明：`docs/CORE_BOOTSTRAP_LAYER_GUIDE.md`
2. Task 层详解：`Code/01_Task/TASK_LAYER_GUIDE.md`
3. Task 资源映射：`Code/01_Task/TASK_RESOURCE_MAP.md`
4. Module 层详解：`Code/02_Module/MODULE_LAYER_GUIDE.md`
5. Common 层详解：`Code/03_Common/COMMON_LAYER_GUIDE.md`
6. Driver 层详解：`Code/04_Driver/DRIVER_LAYER_GUIDE.md`

---

## 3. 新人快速上手路径

1. 确认主流程：`main.c -> MX_FREERTOS_Init -> InitTask -> 业务任务`。
2. 看 Task 层任务表和同步资源表，建立“谁触发谁”的全局图。
3. 按业务链路追 Module 调用，再追到 Driver。
4. 调参或协议改造时最后再看 Common 层公式与工具细节。

---

## 4. 维护约定

1. 任意层接口变更后，必须同步更新对应层文档。
2. 新增任务/模块/驱动后，必须更新本索引清单。
3. 若调整分层边界，必须在文档里说明“迁移前后职责变化”。
