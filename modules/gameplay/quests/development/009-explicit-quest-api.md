# 009：任务 API 明确用途

- 日期：2026-10-24；状态：已实现，常规完整回归 43/43 通过。
- 导航：[本系统索引](README.md) · [玩家接口更名](../../player/development/004-explicit-business-api-names.md)。

## 目标与取舍

QuestBook 与 Player 的同类任务动作保持一致可读性。query 改 query_quest_status，accept 改 accept_quest，apply 改 apply_progress_event，observe 改 observe_gameplay_fact，accepted 改 find_accepted_quest。Player 层进度入口使用 apply_quest_progress_event，强调跨业务协调器中的目标对象。

进度事件与原始事实是不同输入，不能统一成模糊 update；新名称保留二者区别。未修改目标处理器 validate/update/complete 等已有明确接口。

## 实现与边界

迁移声明、实现、内部调用、玩家协调、测试和示例，更新当前 README；不改变冻结定义、事实绑定、事件重放和快照恢复。旧短名不保留，原步骤是历史依据，当前名称以本记录为准。

## 验证与限制

复用现有核心、事实、恢复、配置与分配失败测试，本次没有新增业务规则。最终环境与其余检查见玩家命名记录；不能据此声称已有压测或服务端持久性保证。
