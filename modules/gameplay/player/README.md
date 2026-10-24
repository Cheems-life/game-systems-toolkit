# 玩家业务协调

状态：实验性C++17，独立于引擎。Player协调一个玩家的业务状态，不是账号服务或Godot角色。

## 状态与配置的所有权

```text
可信应用 / 配置包
  商品ProductCatalog
  交互interaction::Catalog
  对话dialogue::Catalog / 正文TextTable
          ↓ 调用期只读借用
Player
  state_
    assets：唯一Economy（钱包、容器、经济记录）
    purchases：PurchaseBook（购买记录）
    quests：QuestBook（任务、冻结规则、事件、领奖记录）
    interactions：interaction::Book（交互记录）
    dialogues：dialogue::Book（冻结会话与命令记录）
```

Player不保存当前商品、交互或对话目录，也不保存它们的引用。Book仍属于玩家，参与候选状态、统一提交与恢复；已接任务与活动对话的冻结定义仍保留。任务目录及物品/货币规则内部所有权本次未调整。

链接目标 `designer::player` 组合 quests、commerce、interaction、dialogue；领域模块不链接Player。文件写入另由应用调用persistence。头文件：[player.hpp](include/designer/player/player.hpp)、[archive.hpp](include/designer/player/archive.hpp)。

## 使用入口

```cpp
// 目录来自可信配置，并在本次同步调用期间有效且不会被并发修改。
const auto bought = player.purchase_product(products, purchase_request);
const auto hint = player.check_interaction_availability(interactions, point);
const auto inspected = player.execute_interaction(interactions, interaction_request);
const auto spoken = player.execute_dialogue_command(dialogues, dialogue_command);
// 调用方检查各返回值，不把调用当作成功。
```

构造：`Player(quests, registry, assets, event_limit=256, purchase_limit=256, interaction_limit=256, dialogue_limit=256)`。所有上限作用于运行记录，不是配置表大小；独占接管Economy，空指针拒绝。

| 入口 | 用途 |
| --- | --- |
| query_quest_status / accept_quest | 查询与接取 |
| apply_quest_progress_event / observe_gameplay_fact | 明确目标事件 / 可信玩法事实 |
| purchase_product(products, request) | 借用商品目录，资产与购买记录一起提交 |
| claim_quest_reward(request) | 发冻结奖励、标记领取、记录凭证一起提交 |
| check_interaction_availability(catalog, point) | 本地交互规则与事件空间提示，不承诺后续任务投递一定成功 |
| execute_interaction(catalog, request) | 新交互与任务事实统一提交 |
| execute_dialogue_command(catalog, command) | 推进对话，finish与完成事实统一提交；cancel不产生事实 |
| economy / quests / interactions / dialogues | 只读访问当前运行状态 |
| snapshot / restore | 整体快照及校验恢复 |
| replace_quests | 当前任务目录替换，不修改已接冻结定义 |

新请求按本次传入目录处理；成功请求先查历史，空目录仍可重放。旧会话的非start命令使用冻结图，因此即使当前目录删除该图，仍可继续或取消。新start需要目录中的当前图。查看和执行可传不同配置，应用须明确发布边界。

## 统一提交与事实边界

玩家用候选State操作，准备记录及返回结果后交换一次。失败不提交正式资产、任务、交互或会话。经济请求与交互/对话请求分别去重；任务事实共用可信递增事件序号。

交互按quests.event_cursor()+1分配事实，投递失败不消耗一次性交互。对话finish按同一流生成冻结完成事实，投递失败保持原会话。外部拾取发物品与observe_gameplay_fact仍是两个调用，除非可信业务另行统一，不能视为联合事务。

所有访问单线程。成功玩家提交后旧只读引用/指针可能失效，跨操作保留数据请取快照。候选深复制的性能尚未压测；没有数据库、网络或外部场景副作用事务。

## 快照与格式

PlayerSnapshot包含commerce::Snapshot（唯一经济快照与购买记录）、BookSnapshot、interaction::Snapshot、dialogue::Snapshot。当前配置目录不写入快照；会话冻结图是历史运行依据，会保存。

**当前 DQSTARCH 写 v4，读 v1/v2/v3/v4。** 在 v3 交互列表后、外层 FNV 校验前追加 dialogue_size u32 和独立 DDLGARCH v1 字节段；该段最多 4 MiB，外层仍限 16 MiB。v1..v3 补空对话状态，读取后保存升级 v4；旧程序不能读取新版，无写回旧格式功能。内层图/会话/日志的详细格式见 [对话模块](../dialogue/README.md)。

对话自身通过冻结定义重放命令验证；编解码和 Player::restore 都验证每条完成回执存在同 ID/类型/对象/数量的原始任务事实，并拒绝与交互或另一完成回执占用同一事实。完整恢复再重算任务进度。恢复不读取当前对话目录、不重复场景或网络副作用。允许外部事实没有对话归属，因此校验不是认证，也不保证识别恶意删除全部归属记录。


应用替换自己的Catalog后只影响未来查询/新操作，不原地修改旧会话。正文未冻结在会话中，旧文本键保留由应用内容发布负责。保存与内存提交是不同边界：保存失败重试保存，不重新发奖。

## 源码迁移

- 构造函数删掉商品、交互、对话Catalog参数，仅保留任务规则、资产和各历史上限。
- purchase_product/check_interaction_availability/execute_interaction/execute_dialogue_command新增对应只读Catalog参数。
- replace_products/replace_interactions/replace_dialogues已移除，配置发布由应用完成。
- State::assets是玩家资产成员，公开economy()及快照economy字段保持不变。
- 独立Shop仍可拥有自己的商品目录；旧源码接口不提供隐式兼容重载。

## 运行与设计记录

直接体验[新手村应用](../../../apps/console/README.md)。独立示例仍在examples/interaction、examples/dialogue，示例目录更新由局部变量管理。

[完整开发记录](development/README.md) · [当前所有权决策008](development/008-external-interaction-dialogue-catalogs.md) · [架构图](../../../docs/architecture-details.md) · [质量要求](../../../docs/quality.md)。
