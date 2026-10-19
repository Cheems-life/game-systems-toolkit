# 玩家业务协调

状态：实验性 C++17 实现，从任务模块提取。这里的 Player 是高层业务协调器，不是引擎角色、账号或网络会话。

## 依赖与所有权

```text
designer::player
  ├── designer::quests（任务核心，也可用 designer::quest_core）
  └── designer::commerce
        └── designer::economy → designer::inventory
```

Player 持有唯一 Economy、无资产的 PurchaseBook 和 QuestBook。购买、领奖和整体恢复通过同一候选状态提交。领域模块不包含 Player 头文件，不链接玩家模块；任务头仅前置声明玩家协调器，并授予这一具体类友元权限，保留受控领奖准备访问，不形成链接循环。

| 类型 | 用途 |
| --- | --- |
| `designer::player::Player` | 协调接取、事件、购买、领奖及整体恢复 |
| `ClaimResult` | 领奖错误、经济/背包错误、凭证与重放标记 |
| `PlayerSnapshot` | 唯一经济快照、购买记录和任务簿快照 |
| `ArchiveError` | 完整玩家存档格式错误 |

任务定义、QuestBook、ClaimRequest、ClaimReceipt、事件和任务错误仍属于 `designer::quests`。例如 `Player::accept/restore` 返回 `quests::Error`；本次不添加无实际需求的统一错误框架。

## 入口

```cpp
#include <designer/player/player.hpp>
#include <designer/player/archive.hpp>
// 构造 designer::player::Player 后，统一调用 purchase / claim。
```

CMake 链接 `designer::player`；写文件再链接 `designer::persistence`。任务核心单独使用时只链接 `designer::quests` 或 `designer::quest_core`。

只读接口 `economy()` / `quests()` 用于查询，不提供可写指针。成功接取、推进、购买、领奖、恢复后旧视图可能失效；跨操作保留数据应使用快照。商品目录与任务目录替换有各自的引用失效边界。

## 存档与迁移

源码迁移：旧 `designer/quests/player.hpp`、`designer/quests/archive.hpp` 改为 `designer/player/player.hpp`、`designer/player/archive.hpp`；Player、ClaimResult、PlayerSnapshot 和完整玩家编码 API 改用 `designer::player` 命名空间；完整集成链接目标从 `designer::quests` 改为 `designer::player`。不提供旧头文件转发，避免任务模块继续隐式带入高层依赖。

DQSTARCH v1 的标识、字段、小端规则和校验均保留，已有文件不需要转换；历史格式记录见 [任务步骤 005](../quests/development/005-player-save-and-restore.md)。当前商品/任务配置不回写冻结历史。内存提交与文件保存仍是两个边界，不提供数据库事务、多线程、网络副作用、自动存档或防回滚保证。

现有集成示例保留 `designer_quests_example` 名称与 `examples/quests` 路径；集成测试保留 `tests/quests` 路径，用于持续回归，不影响模块归属。运行方式见 [任务说明](../quests/README.md)。

## 开发记录

- [开发步骤索引](development/README.md)
- [工程质量要求](../../../docs/quality.md)
