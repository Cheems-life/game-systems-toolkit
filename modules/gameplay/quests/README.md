# 任务与目标：第一版

状态：实验性 C++17 实现。支持通用数字目标、等级门槛、冻结规则、批量事件、一次性领奖及商城/任务共享玩家状态。独立于 Godot，没有击杀、采集、对话等玩法核心字段。

## 使用入口与依赖

- `designer::quest_core`：`quests.hpp`，包含目标处理器、等级求值、任务定义和 QuestBook；依赖 economy 的奖励类型，不链接 commerce。
- `designer::quests`：`player.hpp` / `archive.hpp`，提供共享玩家协调与完整存档；组合 quest_core、commerce 和 economy。
- 文件保存按需链接 `designer::persistence`，领域库不直接读写文件。

```text
Player 的候选状态
├── Economy：唯一的钱包、容器与经济历史
├── PurchaseBook：仅保存购买记录，不另持有钱包
└── QuestBook：冻结任务、目标进度、事件与领奖记录
```

`Player::purchase` 与 `Player::claim` 操作同一经济对象。已有独立 `Shop` 接口仍可使用，内部也复用 PurchaseBook；不要把两个独立对象当作同步的同一个玩家。需要组合时使用 Player 入口。任务核心本身没有商城类型依赖。

## 定义与冻结

`QuestDefinition` 包含非零 ID/版本、最低等级、目标列表、奖励、组合策略和可选标题。标题仅展示，不参与规则，最大 128 字节。每个任务 1–32 个目标，目标 ID 在任务内唯一；任务目录最多 128 项。奖励至少有一项，货币与物品列表各最多 64 项，结构非法在构造时拒绝。

目录排序目标和奖励，拒绝重复条目、零数量及非法 ID。奖励货币/物品跨表引用最终由经济执行校验；第一版没有完整离线配置检查器。

接取复制目标、奖励及版本作为冻结定义，初始进度为零。后续改配置或移除任务，不改变已接任务；恢复只要求对应处理器及当前资产规则可用，不要求当前任务目录仍含旧任务。

任务状态为未解锁、可接取、进行中、可领奖、已领奖。前两项动态求值；后续状态从已接目标进度与 claimed 标记派生。任务一次性，重复接取返回 already_accepted，不清空进度。

## 等级条件：布尔、原因与错误

`evaluate(ConditionSpec, PlayerReadContext)` 是纯函数。当前只支持 `level >= minimum_level`，两者必须为正数。

| 结果 | allowed | 原因与错误 |
| --- | --- | --- |
| 满足 | true | reason=none，error=none |
| 等级不足 | false | reason=level_too_low，含 required/actual |
| 无效上下文或定义 | false | 独立 ConditionError，不冒充普通不满足 |

`query` 与实际 `accept` 都求值；查询结果不是接取凭证。等级由可信玩家上下文提供，不接受客户端任意上报；任务不实现升级或保存第二份等级。接取后即使降级，推进和领奖也不重新检查最低等级。

当前没有前置任务、时间或世界标记条件，任务领奖不会自动开启另一任务。未来扩展条件求值，不向状态机添加具体条件分支。

## 目标注册与组合边界

ObjectiveRegistry 将非零 ObjectiveTypeId 映射到 `shared_ptr<const ObjectiveHandler>`。add 拒绝重复/空处理器，最多 64 项；QuestBook 接管自己的注册表副本，之后不暴露修改入口。处理器必须无状态副作用、确定性、noexcept；不能写外部对象、做 I/O 或热替换旧语义。

处理器提供 validate、update、complete。默认 `default_objectives()` 装配：

- type=1，rule_version=1：累计计数。target>0，正数输入按增量推进，在 target 封顶，避免加法溢出。
- type=2，rule_version=1：永久标记。target=1，输入只能为 1，进度为 0/1，重复设置无额外变化。

第一版数据载体是 uint64 数字进度，要求 `0 <= progress <= target`，更新单调不减。注册可以扩展这类行为，测试中包含自定义双倍计数处理器；复杂对象状态需未来新契约/版本，不宣称已支持任意目标类型。

配置遇到未知类型/不支持版本会拒绝，恢复也失败而不跳过目标。组合判定在独立的 complete 边界，当前 `CompletionPolicy::all` 要求全部目标达成。任意达成、分阶段涉及组合及激活规则，尚未实现，不能只靠增加叶子处理器获得。

“当前持有若干物品”是可回退条件，不属于本版累计获得；玩法系统与适配层负责把已确认事实转换成目标 ID/数值更新。

## 事件协议与时间边界

`ProgressEvent { id, updates }`，每项为任务 ID、目标 ID、正数值。id 是同一玩家的可信递增事件序号，不是任意随机 UUID；与经济请求 ID 属于不同命名空间。

- 首次处理必须晚于已成功处理的事件序号；未知旧序号返回 stale_event，不支持乱序重排。
- 相同序号、相同规范化内容返回 replayed；内容不同返回 event_conflict。
- 更新按任务/目标排序；同批重复目标拒绝，适配层先聚合；最多 64 项。
- 一次事件可更新多个任务。目标不存在、任务未接/已领奖、标记输入不合法等，整批失败，不推进任何目标，也不占用序号和记录。
- 可领奖但未领取的目标仍封顶/保持标记；已领奖的新更新拒绝。旧成功事件的原样重放仍允许。
- 空更新批次合法，可记录一个已观察但未匹配任务的事实。

接取上下文为 `PlayerReadContext{level, event_cursor}`。cursor 表示可信来源确认的接取边界，不得早于已处理事件或已有任务接取边界；不能为了接任务伪造/倒退它。该任务只接受 `event.id > accepted_after` 的更新，不会补算接取前事实。来源必须保证序号忠实对应事实发生先后；核心不能识别来源把旧事实重新编号后的造假。

事件默认最多记录 256 条，可配置 1–1024；满后拒绝新事件，保留旧记录用于重放，不静默清理。任务数量（包括已领奖和已从目录移除的任务）最多 128。该有界策略不适合无限期运营，长期保留/归档策略待设计。

## 领奖准备列表与唯一提交

领取请求为 `{request_id, quest, destination}`，均非零。Player 先查成功领奖历史，再判断任务已接、未领奖、全部目标达成。

```text
建立同一个玩家候选状态
  → prepare_rewards：在候选 Economy 中发全部冻结奖励
  → prepare_claimed：只将候选任务标记为已领奖
  → prepare_receipt：记录候选领奖回执
  → 准备返回结果
  → 交换一个玩家状态指针
```

三个步骤以明确的 ClaimPreparation 函数列表组织；未来可在同一候选中增加世界状态准备，但本版没有世界修改或运行时事务插件。任何错误、分配失败都丢弃候选，真实资产/任务/记录均不变；不在准备阶段发送网络、写数据库或调用场景回调。

同请求同意图重试返回原回执，同请求换任务/目标容器报冲突；换新 ID 也不能领取已领奖任务。余额上限、背包容量或经济历史上限失败时，仍为可领奖，可处理条件后重试。

商城购买和领奖共享经济请求 ID 空间，不能复用另一业务的 ID；已存在经济交易但没有对应领奖记录时也不收编为领奖。成功经济回执是事实记录，不是任务领奖资格。

## 快照、校验与存档

- QuestBook::snapshot/restore 支持冻结定义、目标进度、全部事件和领奖记录。因为事件未淘汰，恢复会从零重建进度并核对存储数值，使用当前注册处理器的相同规则。
- PlayerSnapshot 仅有一份经济数据（嵌入 commerce 快照），同时保存购买和任务信息。恢复先在候选验证资产、购买关联、任务与事件，再核对领奖经济回执，最后一次提交。
- 结构解码不调用目标插件；未知正数类型/版本可以解析，但 restore 必须有兼容处理器。解码成功不代表已通过当前领域规则。
- 已领取标记与领奖记录必须一一对应，奖励与冻结定义相同，购买/领奖不能共用同一经济回执。

新文件 `DQSTARCH` v1，固定小端、16 MiB 上限：

```text
magic[8], version:u32=1, commerce_size:u32, commerce_bytes
accepted_count:u32
  quest_id:u32, version:u32, minimum_level:u32, policy:u32
  title_size:u32, title_bytes, accepted_after:u64, claimed:u8
  objective_count:u32
    id:u32, type:u32, rule_version:u32, target:u64, progress:u64
  currency_reward_count:u32, (currency:u32, amount:u64)...
  item_reward_count:u32, (item:u32, quantity:u32)...
event_count:u32
  event_id:u64, update_count:u32, (quest:u32, objective:u32, value:u64)...
claim_count:u32
  request_id:u64, quest:u32, destination:u32, economic_sequence:u64
checksum:u64 = FNV-1a of preceding bytes
```

序列均有明确上限；任务按 ID、事件按序号、领奖按经济序号递增，拒绝非法/重复关联、尾部垃圾、截断和未来版本。编码/解码只在成功时替换输出。FNV-1a 不是认证，无防回滚/防伪造保证；重建进度也不能证明外部事实真实。

不更改 DINVARCH、DECOARCH、DSHPARCH 旧格式。可把旧 Shop 快照与空 BookSnapshot 组成 PlayerSnapshot 后由新 Player 验证恢复；不会凭空生成任务。没有旧任务格式，暂未编写任务迁移器。

保存与内存成功是不同边界，保存失败重试保存，不重做发奖。多人共享世界、并发、数据库持久性及可靠外部投递仍需后续设计。当前资产规则变化也可能使旧奖励/经济记录恢复失败，需要真实迁移方案。

## 运行与阅读顺序

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/examples/designer_quests_example build/example-quests.sav
./build/examples/designer_quests_example build/example-quests.sav --load
```

首条示例替换指定示例文件。它模拟计数和标记两个适配来源，验证等级门槛、降级后继续、配置冻结、商城消费与领奖，以及事件/购买/领奖重放。第二条进程用空商品/任务目录恢复。不要指向需要保留的真实存档。

先读 [目标与任务核心](include/designer/quests/quests.hpp)，再读 [共享玩家协调](src/player.cpp) 的候选准备与提交。测试位于 [tests/quests](../../../tests/quests/)。

## 验证与限制

测试包含纯核心（不链接商城）、自定义处理器、等级/事件边界、多目标失败原子性、共享资产、冻结奖励、换请求 ID 不重复领奖、完整存档/重建、固定格式样本、2000 次解析变异、逐分配点 bad_alloc 和双进程恢复。实际运行结果记录在下方开发步骤中，不把接口存在当商业级保证。

所有访问按单线程处理。Player 成功接取/推进/购买/领奖/恢复后旧资产和任务视图失效，取快照跨操作保存。内存候选深复制（包括历史及配置）易于验证但有明显成本，未压测。没有取消、重置、任务分支、任意组合、阶段、世界修改、编辑器或 UI。

## 开发过程与决策

[开发记录](development/README.md) 保留最初需求步骤 001，本次 002–005 说明具体接口、共享边界、格式、实际验证和限制。商业级标准见 [质量要求](../../../docs/quality.md)。
