# 玩家业务协调

状态：实验性 C++17 实现，从任务模块提取。这里的 Player 是高层业务协调器，不是引擎角色、账号或网络会话。

## 依赖与所有权

```text
designer::player
  ├── designer::interaction（独立交互规则）
  ├── designer::quests（任务核心，也可用 designer::quest_core）
  └── designer::commerce
        └── designer::economy → designer::inventory
```

Player 持有唯一 Economy、无资产的 PurchaseBook、QuestBook 和 interaction::Book。购买、领奖和整体恢复通过同一候选状态提交。领域模块不包含 Player 头文件，不链接玩家模块；任务头仅前置声明玩家协调器，并授予这一具体类友元权限，保留受控领奖准备访问，不形成链接循环。

| 类型 | 用途 |
| --- | --- |
| `designer::player::Player` | 协调接取、事件、购买、领奖及整体恢复 |
| `ClaimResult` | 领奖错误、经济/背包错误、凭证与重放标记 |
| `PlayerSnapshot` | 唯一经济快照、购买记录和任务簿快照 |
| `ArchiveError` | 完整玩家存档格式错误 |

任务定义、QuestBook、ClaimRequest、ClaimReceipt、事件和任务错误仍属于 `designer::quests`。例如 `Player::accept_quest/restore` 返回 `quests::Error`；本次不添加无实际需求的统一错误框架。

## 入口

```cpp
#include <designer/player/player.hpp>
#include <designer/player/archive.hpp>
// 构造 designer::player::Player 后，统一调用 purchase_product / claim_quest_reward。
```

CMake 链接 `designer::player`；写文件再链接 `designer::persistence`。任务核心单独使用时只链接 `designer::quests` 或 `designer::quest_core`。

只读接口 `economy()` / `quests()` 用于查询，不提供可写指针。成功接取、推进、购买、领奖、恢复后旧视图可能失效；跨操作保留数据应使用快照。商品目录与任务目录替换有各自的引用失效边界。

## 存档与迁移

源码迁移：旧 `designer/quests/player.hpp`、`designer/quests/archive.hpp` 改为 `designer/player/player.hpp`、`designer/player/archive.hpp`；Player、ClaimResult、PlayerSnapshot 和完整玩家编码 API 改用 `designer::player` 命名空间；完整集成链接目标从 `designer::quests` 改为 `designer::player`。不提供旧头文件转发，避免任务模块继续隐式带入高层依赖。

提取协调器时曾保留 DQSTARCH v1 字节格式；事实绑定步骤曾升级为 v2；当前世界交互扩展写 v3，读取 v1/v2/v3，详见下文。历史 v1 格式记录见 [任务步骤 005](../quests/development/005-player-save-and-restore.md)。当前商品/任务配置不回写冻结历史。内存提交与文件保存仍是两个边界，不提供数据库事务、多线程、网络副作用、自动存档或防回滚保证。

现有集成示例保留 `designer_quests_example` 名称与 `examples/quests` 路径；集成测试保留 `tests/quests` 路径，用于持续回归，不影响模块归属。运行方式见 [任务说明](../quests/README.md)。

## 开发记录

- [开发步骤索引](development/README.md)
- [工程质量要求](../../../docs/quality.md)

## 事实投递与存档 v2

`Player::observe_gameplay_fact(quests::GameplayFact)` 把可信事实交给候选 QuestBook，成功后交换一次玩家状态，失败保留所有资产和任务。领取带绑定的任务时，同一候选内保存 `AcceptedQuest::fact_until`，与发奖、已领奖标记、凭证一起提交。它记录已处理到的最大事件序号，空历史为 0；只用于历史路由重建，不是现实时间或外部世界时间。

事实绑定步骤曾输出 **DQSTARCH v2**；当前交互步骤已升级为 v3，读取 v1/v2/v3，详见下文。v1 缺失字段解释为“目标无绑定、事件无原始事实、fact_until 为 0”，不会根据新配置自动改写旧任务。读取后再次保存升级为当前 v3，旧程序无法读取；本模块不提供写回 v1 或磁盘备份。

相对 v1，v2 的字段顺序增量如下，均为小端：

- 每个已接任务：在 claimed u8 后增加 fact_until u64。
- 每个目标：在 progress u64 后增加 binding_present u8（仅 0/1）；存在时写 fact_type u32、subject u32、input u32（quantity=1、one=2）。
- 每个事件：在全部 updates 后增加 fact_present u8（仅 0/1）；存在时写 type u32、subject u32、quantity u64；事实 ID 就是外层 event.id，不重复保存。
- 其余嵌套商城/经济字节、领奖引用及外层 FNV-1a 不变，完整文件仍限 16 MiB。

结构编解码校验字段、标记、基本关闭边界及关联；`Player::restore` 再按冻结绑定、历史接取/领奖边界和注册处理器重算完整匹配集合及进度。只有全部通过才提交。没有数据库事务、多线程原子操作、事实认证或世界副作用。尤其“拾取发物品”和“提交任务事实”仍是两个外部步骤；业务接入方需要定义投递失败后的重试/持久化策略，不能因为使用同一个 Player 就视为联合事务。

```sh
./build/examples/designer_quest_facts_example examples/configuration/bindings.json build/facts-demo.sav
./build/examples/designer_quest_facts_example examples/configuration/bindings.json build/facts-demo.sav --load
```

第一条只替换指定示例存档。第二条使用空当前任务/商品目录恢复冻结历史并重放。详细设计及验证见 [步骤 002](development/002-fact-delivery-and-archive-v2.md)。

## 世界交互与存档 v3

Player 现在额外持有独立 interaction::Book 和当前交互目录。构造函数在原 event_limit、purchase_limit 后追加交互 Catalog（默认空）和 interaction_limit（默认 256），旧源码调用仍有效。新入口为 check_interaction_availability、execute_interaction、interactions 只读查询及 replace_interactions。

execute_interaction 先查冻结成功记录；新请求检查本地交互规则和事实序号溢出，随后在候选内执行交互，再 observe_gameplay_fact 投递固定事实，准备结果后一次提交。任务拒绝时返回 interaction::Error::fact_rejected 和 quest_error，交互没有消耗。check_interaction_availability 是本地提示，不预运行任务处理器，实际执行仍可能因任务日志满/匹配超限失败。

事件序号为 QuestBook.event_cursor()+1；cursor 包括成功事件和全部接取边界。交互请求 ID 与经济请求 ID 是独立域，但生成事实与外部直接更新/事实共用同一事件序号空间。交互本身无资产效果；任务奖励仍经claim_quest_reward 入口领取。

**当前写 DQSTARCH v3，读 v1/v2/v3。** v3 保留 v2 所有字段，在领奖列表之后、外层校验之前追加：交互数 u32，随后每条 request_id u64、point u32、version u32、mode u32（once=1、repeatable=2）、fact_type u32、event_id u64。仍为小端/FNV-1a/16 MiB。旧格式补空交互记录，下次保存升级 v3；旧读取端不能读取 v3。

结构编解码和完整恢复都校验每条交互凭证对应同 ID、类型、对象=point、数量=1 的原始任务事实，且交互簿内部满足请求/模式/一次性约束。历史恢复不使用当前交互目录。允许外部事实没有交互凭证，因此这些关联检查不是认证，也不能保证恶意删除归属记录可被识别。

独立 Book.execute_interaction 提交与任务 observe_gameplay_fact 不是同一个事务；只有 Player 候选将二者组合。没有数据库、多线程或外部场景回调保证。新增模块边界、使用方式见 [交互模块](../interaction/README.md)，设计与验证见 [步骤 003](development/003-interaction-and-archive-v3.md)。

## 当前业务入口

| 接口 | 用途 |
| --- | --- |
| query_quest_status | 查询任务状态与接取条件 |
| accept_quest | 正式接取任务 |
| apply_quest_progress_event | 提交明确任务/目标的进度事件 |
| observe_gameplay_fact | 根据可信事实匹配目标绑定并推进 |
| purchase_product | 购买商品 |
| claim_quest_reward | 一次性领取任务奖励 |
| check_interaction_availability | 检查新交互资格及共享序号上限 |
| execute_interaction | 执行交互并在候选中投递事实 |

短名接口已移除，源码迁移见 [命名步骤 004](development/004-explicit-business-api-names.md)。参数、失败语义和存档格式均不变。
