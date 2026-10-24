# 玩家业务协调：开发记录

状态：实验性，原有玩家能力已从任务模块提取。

[模块说明](../README.md) · [记录规范](../../../../docs/development-records.md)

- [001：从任务模块提取玩家协调器](001-extract-player-coordinator.md)

原始设计保留在 [任务步骤 004](../../quests/development/004-player-coordination-and-claim-plan.md) 与 [步骤 005](../../quests/development/005-player-save-and-restore.md)，本次调整覆盖其中的模块归属约定，不改变业务规则。

- [002：事实投递与玩家存档 v2](002-fact-delivery-and-archive-v2.md)

- [003：交互、任务事实与完整存档 v3](003-interaction-and-archive-v3.md)

- [004：明确业务接口命名](004-explicit-business-api-names.md)：2026-10-24，当前接口名与迁移说明。

- [005：对话完成事实与玩家格式 v4](005-dialogue-and-archive-v4.md)：对话/任务候选提交、冻结会话存档及旧格式兼容。

- [006：商品目录外置](006-external-product-catalog.md)：购买借用可信配置，保留交互/对话目录并更新架构图。

- [007：明确玩家资产成员命名](007-player-assets-name.md)：State::economy改为assets，公开接口与存档格式不变。
