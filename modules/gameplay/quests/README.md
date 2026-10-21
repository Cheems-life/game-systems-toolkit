# 任务与目标

状态：实验性 C++17 实现。支持等级门槛、计数/标记目标、冻结事实绑定、任务进度、一次性领奖和存档，独立于 Godot。

## 先理解整体关系

```text
QuestCatalog（任务配置表）
    ↓ 接取时复制规则
QuestBook（玩家任务簿）
    ↓ 接收 ProgressEvent，更新目标进度
任务完成，等待领奖
    ↓ Player 协调任务与 Economy
发奖励 + 标记已领奖 + 记录凭证，统一提交
```

`QuestBook` 管任务，`Economy` 管资产，独立 [玩家模块](../player/README.md) 的 `Player` 把任务领奖、商城购买与同一份资产协调起来。

## 数据结构与用途

下表按使用过程排列。先理解“任务配置 → 玩家进度 → 事件更新 → 领奖 → 存档”，再看条件与扩展接口。

| 分组 | 类型 | 主要内容 | 用途 / 如何理解 |
| --- | --- | --- | --- |
| 身份 | `QuestId` / `ObjectiveId` / `ObjectiveTypeId` | 任务 ID / 目标 ID / 目标类型 ID | 分别回答“哪个任务”“其中哪个目标”“用哪种计算规则”。 |
| 身份 | `EventId` | 玩家可信递增事件序号 | 标识一次事实事件，用于顺序检查和去重，不是经济请求 ID。 |
| 任务配置 | `ObjectiveDefinition` | 目标 ID、类型、规则版本、目标值 | 一个目标的规则，例如累计达到 10。 |
| 任务配置 | `QuestDefinition` | ID、版本、等级要求、目标、奖励、完成策略、标题 | 一个任务的完整规则，不记录玩家进度。 |
| 任务配置 | `QuestCatalog` | 任务 ID → 定义 | 所有当前任务的配置表，用于查询与接取。 |
| 玩家进度 | `AcceptedQuest` | 冻结定义、接取事件边界、进度数组、已领奖标记 | 玩家实际接下的任务；进度按排序后的目标一一对应。 |
| 玩家进度 | `QuestBook` | 已接任务、已处理事件、领奖记录 | 管理一个玩家的任务状态，提供查询、接取、推进和恢复。 |
| 状态查询 | `Status` | locked / available / active / ready / claimed | 未解锁 / 可接 / 进行中 / 可领奖 / 已领奖；由条件、进度和标记得出。 |
| 状态查询 | `QuestView` | 错误、状态、条件结果 | 一次查询的返回值，不是接取凭证。 |
| 接取条件 | `PlayerReadContext` | 等级、事件游标 | 调用方提供的可信玩家信息；游标界定接任务前后的事件。 |
| 接取条件 | `ConditionSpec` | 最低等级 | 描述接取条件，不保存玩家状态。 |
| 接取条件 | `ConditionResult` | 是否允许、原因、错误、要求值、实际值 | `evaluate` 的结果，例如要求 10 级、当前 8 级。 |
| 接取条件 | `ConditionReason` / `ConditionError` | 等级不足 / 非法上下文或定义 | 区分正常“不满足条件”和数据本身有错误。 |
| 目标扩展 | `ObjectiveHandler` | validate / update / complete | 目标算法接口：检查状态、计算新进度、判断完成；必须确定且无副作用。 |
| 目标扩展 | `ObjectiveRegistry` | 类型 ID → 处理器 | 根据目标类型寻找算法；`default_objectives()` 提供默认计数和标记规则。 |
| 目标组合 | `CompletionPolicy` | 当前只有 all | 要求所有目标完成，不支持任意完成或分阶段。 |
| 事件推进 | `ObjectiveUpdate` | 任务 ID、目标 ID、输入值 | 更新一个目标；`value` 是算法输入，不一定是最终进度。 |
| 事件推进 | `ProgressEvent` | 事件 ID、更新列表 | 一次事实可推进多个目标，打包提交并去重。 |
| 事件推进 | `EventResult` | 错误、是否重放 | 告知事件是否成功，是否已经处理过。 |
| 领奖 | `ClaimRequest` | 请求 ID、任务 ID、目标容器 | 表达“领取哪个任务，物品放哪里”。 |
| 领奖 | `ClaimReceipt` | 领奖请求、经济结算凭证 | 成功领奖的记录，把任务身份与实际奖励关联起来。 |
| 领奖 | `designer::player::ClaimResult` | 各层错误、可选凭证、是否重放 | `Player::claim` 的返回值；失败时可定位经济或背包原因。 |
| 统一协调 | `Player` | 经济状态、购买记录、任务簿 | 同一个玩家只有一份资产；购买和领奖都从这里操作。 |
| 存档 | `BookSnapshot` | 已接任务、事件历史、领奖凭证 | 任务簿的独立数据副本，不含钱包和背包。 |
| 存档 | `designer::player::PlayerSnapshot` | 商城/经济快照、任务簿快照 | 一起保存资产、购买、任务和领奖状态，避免只恢复一半。 |
| 错误 | `quests::Error` / `player::ArchiveError` | 领域错误 / 文件格式错误 | 前者用于任务操作，后者用于存档编码解码。 |

`Player::State`、`ClaimPreparationContext` 和 `ClaimPreparation` 是协调器的私有实现：分别保存候选状态、传递领奖准备数据、组织准备步骤，普通调用方不需要创建。

## 一个例子

任务 100 要求 5 级接取、累计击杀 10 次，奖励 100 金币：

1. 在 `QuestCatalog` 配置 `QuestDefinition`，其中包含一个计数型 `ObjectiveDefinition`。
2. 调用 `Player::accept`，传入可信等级与事件游标，建立 `AcceptedQuest`，初始进度为零。
3. 玩法逻辑确认击杀后，构造 `ProgressEvent`，其中 `{100, 1, 1}` 表示给任务 100 的目标 1 输入一次计数。
4. 调用 `Player::apply` 推进进度；达到目标后，查询状态为 `ready`。
5. 调用 `Player::claim`。奖励到账与已领奖标记一起提交；若背包满等条件导致失败，仍可领奖。
6. 使用 `PlayerSnapshot` 保存完整玩家状态；恢复后保留进度、资产和去重记录。

## 必须记住的规则

- 接取时冻结任务定义；改配置不会改变已接任务的目标和奖励。任务一次性，不支持取消、重置或重复接取。
- 默认计数目标按正数增量推进并在目标封顶；标记目标使用 0/1。当前仅支持单调数字进度，不支持会回退的“当前持有数量”。
- 事件必须来自可信来源并按递增序号处理，只计算接取边界之后的事实；同 ID 同内容重放不会重复推进。一个批次全部成功或全部不变。
- 完成不等于已领奖。`Player` 保证发奖、已领奖标记与凭证一起提交；换请求 ID 也不能重复领取。购买和领奖不能复用同一经济请求 ID。
- 配置最多 128 个任务，每任务最多 32 个目标；事件批次最多 64 项，事件记录默认 256、最多 1024 条。历史满后拒绝新事件，尚无长期归档策略。
- 完整玩家存档写 `DQSTARCH` v2、读 v1/v2，最大 16 MiB。解码只检查结构，恢复再检查规则及事件重建；校验值不是加密或防作弊。保存失败应重试保存，不重做发奖。
- 当前为单线程内存协调，没有并发、数据库事务或性能保证。`Player` 成功修改后旧资产与任务视图失效，跨操作保留数据使用快照。

## 代码与运行入口

- [quests.hpp](include/designer/quests/quests.hpp)：上表的大部分任务类型，目标为 `designer::quests`（也可用 `designer::quest_core`），不链接商城或玩家模块。
- [player.hpp](../player/include/designer/player/player.hpp)：独立玩家协调器与领奖结果，目标为 `designer::player`，组合任务、商城与经济。
- [archive.hpp](../player/include/designer/player/archive.hpp)：玩家模块的完整快照编码/解码；磁盘读写另用 `designer::persistence`。
- [测试目录](../../../tests/quests/)：核心规则、共享资产、去重、恢复与异常验证。详细结果以开发记录为准。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/examples/designer_quests_example build/example-quests.sav
./build/examples/designer_quests_example build/example-quests.sav --load
```

首条示例会替换指定存档，使用测试路径。已有独立 `Shop` 与 `Player` 是不同对象，不会自动同步资产；需要任务与购买共享资产时使用 `Player`。

## 设计原因与详细协议

详细内容按主题查阅，README 只保留学习入口：

- [需求与扩展边界](development/001-requirements-and-extension-boundaries.md)
- [目标处理器与等级条件](development/002-objectives-and-level-conditions.md)
- [冻结规则、事件协议与时间边界](development/003-event-boundaries-and-frozen-progress.md)
- [共享玩家与领奖提交](development/004-player-coordination-and-claim-plan.md)
- [存档布局、恢复规则与验证结果](development/005-player-save-and-restore.md)
- [全部开发记录](development/README.md) · [工程质量要求](../../../docs/quality.md)

玩家协调层提取与源码迁移见 [步骤 006](development/006-extract-player-layer.md)。

## 事实绑定第一版

需要玩法不依赖任务 ID 时，使用 `QuestBook::observe(GameplayFact)`，共享玩家入口为 `Player::observe`：

```cpp
using namespace designer::quests;
// 目标绑定：精确匹配事实类型 100、对象 1001，使用事实数量作为输入。
ObjectiveDefinition goal{10, counter_type, 1, 3,
    ObjectiveBinding{100, 1001, FactInput::quantity}};
// 接取包含该目标的任务后，玩法只提交事实，不指定任务或目标。
auto result = book.observe({101, 100, 1001, 2});
```

| 新类型 / 字段 | 用途 |
| --- | --- |
| `GameplayFact{id,type,subject,quantity}` | 全部为正数的可信事实；编号含义由具体游戏约定 |
| `ObjectiveBinding{fact_type,subject,input}` | 类型和对象都相等才匹配；每目标最多一个可选绑定 |
| `FactInput::quantity / one` | 使用原数量 / 固定输入 1；输入仍须被目标处理器接受 |
| `ObjectiveDefinition::binding` | 随已接任务冻结，当前目录改绑定不影响旧任务 |
| `ProgressEvent::fact` | 可选原始事实；与派生更新一起保存，防止同效果的不同事实被当作相同请求 |
| `AcceptedQuest::fact_until` | 有绑定任务领奖时的最大已处理事件序号；没有已处理事件时为 0，无绑定或未领奖也为 0 |

观察新事实时扫描所有冻结目标，只匹配已接取、未领奖、且事实序号大于接取边界的任务。ready 目标继续接受封顶输入。没有匹配也保存事实；一条事实最多派生 64 项，超过则全部失败，不能拆成同一 ID 的多次调用或截断。任一处理器拒绝输入时，全部目标与历史均不变，失败不占用序号。

相同 ID 同原文返回重放，不重新匹配。因此后来接取、领奖、改表、移除当前目录均不改变旧投递。即使 `one` 模式下数量 1 和数量 9 都生成输入 1，它们仍是不同原文，复用 ID 会冲突。事实与原有直接更新共用一个递增序号空间和历史容量，不能各自从 1 编号。

`apply(ProgressEvent)` 保留为可信低层接口，可手动更新无绑定或有绑定目标；若附带 fact，则必须与当前冻结绑定派生的完整批次相等，不能随意声称更新来自某事实。恢复时用接取/领奖序号边界重算事实批次，再重建进度，检查遗漏和多余更新。原文、绑定和历史自身不是签名，不证明真实世界中发生过该事件。

没有通配、表达式、脚本或内置“拾取/击杀”枚举。不检查 subject 属于物品还是交互对象；玩法端负责编号及语义，例如“获得物品”不是“当前持有数量”。自定义处理器可能在零进度即完成，此时尚无事件的领奖边界为 0。时间边界只在同一可信顺序流内成立。

完整文件示例：[bindings.json](../../../examples/configuration/bindings.json)，实现与理由见 [步骤 007](development/007-fact-bindings.md)。完整玩家格式现在写 DQSTARCH v2，兼容读取 v1，详见 [玩家说明](../player/README.md)。
