# 006：任务核心与玩家协调层分离

- 日期：2026-10-19；状态：已实现，常规回归 23/23 通过。
- 导航：[本系统索引](README.md) · [玩家提取决策](../../player/development/001-extract-player-coordinator.md)。

## 问题与选择

Player 购买、领奖、统一恢复属于跨模块协调，不应使任务模块承载商城。将其连同完整玩家存档移到 gameplay/player，任务保留配置、处理器、任务簿、事件、领奖请求与任务快照。具体理由、步骤和验证见上方玩家记录，避免重复维护。

## 边界与迁移

现在 designer::quests 与 designer::quest_core 均提供任务核心，均不链接 commerce/player。QuestBook 仅前置声明 designer::player::Player 并授权 friend，未包含玩家头，也未暴露可写状态给普通调用者；这是保留的具体协作耦合，不是链接反向依赖。

旧 quests/player.hpp 和 quests/archive.hpp 调用方需换用 player 路径及命名空间，完整集成链接 designer::player。任务事件/进度算法与存档字节未改，不影响已冻结任务。

## 注释检查与验证

用户现有注释与格式修改已核对并保留。构建及完整 CTest 回归通过；行尾空白清理和本地链接检查随本次交付执行。没有新增玩法，未新增规则测试；独立模块引用验证见玩家记录。原步骤 004/005 保留历史，当前模块归属以本记录为准。
