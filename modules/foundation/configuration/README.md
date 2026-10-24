# 配置与数据定义

状态：实验性 C++17 配置加载与校验实现，另有独立对话/语言资源入口。支持 JSON 配置物品、货币、商品和任务，独立于引擎。

## 先理解流程

```text
一份 game.json（四张基础表，v3 另有交互表）
    ↓ 有界读取与严格 JSON 解析
类型、范围、重复、领域规则、目标处理器和跨表引用校验
    ↓ 全部通过后返回完整只读 Bundle
复制 Catalog → 创建 Inventory / Wallet / Shop / QuestBook / Player
```

配置描述“游戏有哪些内容”；钱包、背包和任务簿描述“玩家当前拥有什么/做到哪一步”。配置不是玩家存档，也不包含初始资产、背包格数或玩法事件来源。

## 类型与依赖

| 类型 / 接口 | 用途 |
| --- | --- |
| `Bundle` | 完整校验后的物品、货币、商品、任务目录和目标处理器注册表，只提供 const 查询 |
| `LoadResult` | 成功包含 `unique_ptr<const Bundle>`；数据失败包含 `Diagnostic`，不会同时返回部分包 |
| `Diagnostic` | 错误类型、来源、字段路径、可获得的记录 ID、原因及语法错误字节位置 |
| `decode(bytes, registry, source)` | 从 UTF-8 JSON 字节加载，source 用于诊断 |
| `load(path, registry)` | 有界读文件后调用同一套校验 |

`designer::configuration` 是当前四个领域的配置适配器，依赖 `designer::interaction`、`designer::quest_core`、`designer::commerce`（传递 economy/inventory），私有依赖 persistence 文件读取及 vendored JSON。目录分类不代表依赖层级；领域模块没有反向依赖配置、JSON 或文件系统。使用一个领域时仍可直接创建自己的 Catalog，不必链接本模块。

公共头：[bundle.hpp](include/designer/configuration/bundle.hpp)。JSON 类型不出现在公共 API；固定版本依赖与许可证见 [third_party](../../../third_party/nlohmann_json/README.md)。当前源码/CMake 集成，不提供安装包。

## 基础文件格式（v1/v2/v3）

v1 完整可运行样例见 [game.json](../../../examples/configuration/game.json)。所有字段必填；四张表允许为空，但表内每个商品价格、商品奖励、任务目标列表和任务奖励不能为空。下列正整数均必须使用整数字面量，拒绝 `1.0`、`1e0`、负数、零、字符串和布尔值。

| 位置 | 字段及含义 |
| --- | --- |
| 根对象 | `schema_version: 1、2 或 3`、`content_version: uint64`、`items`、`currencies`、`products`、`quests` |
| items[] | `id: uint32`、`max_stack: uint32` |
| currencies[] | `id: uint32`、`max_balance: uint64` |
| products[] | `id`、`version`（uint32）、`available`（bool）、`unit_cost`、`unit_reward` |
| unit_cost | `currencies: [{currency: uint32, amount: uint64}]` |
| unit_reward / reward | `currencies: [{currency, amount}]`、`items: [{item: uint32, quantity: uint32}]`，各子列表可空，但奖励整体不可空 |
| quests[] | `id`、`version`、`minimum_level`（uint32）、`title`（UTF-8 字符串）、`policy: "all"`、`objectives`、`reward` |
| objectives[] | `id`、`type`、`rule_version`（uint32）、`target: uint64` |

物品/货币/商品/任务各自 ID 唯一；每个任务内目标 ID 唯一，每个价格或奖励子列表内资产 ID 唯一。输入顺序不影响领域目录的既有规范化。未知字段和重复 JSON 键（含转义后同名键）报错。

`schema_version` 决定读取端支持的结构。`content_version` 是发布者提供的内容批次标识；同号不同内容不会自动拒绝，也不提供防回滚或自动迁移。商品版本、任务版本和目标 `rule_version` 分别属于各自领域，不因包版本改变而自动升级。

## 校验与扩展

- 检查价格及所有奖励引用的物品/货币存在，包括下架商品。
- 内置目标类型 `1` 为计数、`2` 为标记，规则版本均为 1；标记 target 必须为 1。自定义处理器先注册到 `ObjectiveRegistry`，再传入加载器；文件仅选择类型与参数，不能定义新算法。
- 等级条件目前只有 minimum_level；完成策略仅 all。添加新数据不需要改业务代码；增加 OR/分阶段等规则仍需实现。
- v1 由玩法适配器手动映射目标；v2 可通过目标 binding 声明精确匹配，详情见下文。
- 引用有效不代表某个玩家一定能完成操作。余额上限、购买数量缩放溢出、背包容量等仍由业务执行检查；本步不计算经济可达性。

上限：输入 4 MiB，JSON 解析深度最大 32（根容器算第 1 层），物品 4096、货币 1024、商品 4096、任务 128，每任务目标 32，价格/奖励的每个资产子列表 64，标题 128 UTF-8 字节。包级上限不改变领域原有 API 的容量。超过 uint64 的整数不会经浮点近似后接受。

## 接入与错误处理

```cpp
#include <designer/configuration/bundle.hpp>
namespace cfg = designer::configuration;

auto result = cfg::load("game.json", designer::quests::default_objectives());
if (result.error) {
    // 展示 source、path、record_id、message，停止创建使用这份配置的业务对象。
    return;
}
const auto& data = *result.bundle;
designer::inventory::Inventory bag(16, data.items());
designer::economy::Wallet wallet(data.currencies());
// Player 接入完整示例见 examples/configuration/main.cpp。
```

检测到的首个错误即停止；结构/规则诊断包含字段或记录路径，例如 `$.products[0].unit_reward.items[0].item`，记录 ID 可用时单独提供。JSON 语法错误使用根路径与 1-based 字节位置；重复 JSON 键、深度/数字解析错误可能只有根路径，不能保证都有记录 ID。诊断消息为英文，调用方可按 Error 做本地化。资源分配失败等异常向上传播，不保证永不抛出。

局部解析和目录随失败销毁，已有包和玩家不变。加载成功不会自动替换运行中对象；不要把逐个调用现有 replace 接口当作四表一次热更新。已有任务规则、购买记录的冻结行为保持不变；配置包版本尚未写入存档。无数据库事务或多线程发布保证。

## 运行

```sh
./build/examples/designer_config_check examples/configuration/game.json
./build/examples/designer_config_example examples/configuration/game.json
./build/examples/designer_config_example examples/configuration/game.json 200 2
ctest --test-dir build -R configuration --output-on-failure
```

校验命令成功退出码 0，数据错误 1，用法错误 2，可由 CI/构建脚本调用；没有自动扫描全仓库配置。命令使用默认目标注册表，自定义游戏需自行接入处理器。集成示例只操作临时内存玩家：提供所选商品的一份初始价格，购买、按所选任务目标模拟输入、领奖并重放。它不是实际玩法事件绑定实现；不合适的商品/奖励组合可能因容量或余额失败。

原资产启动包仍不支持拆分或整包热更新；对话资源的独立加载和显式发布见下文。当前没有 Excel/CSV 导表器、配置签名、存档规则迁移或编辑器。完整取舍与实际验证见 [开发步骤 001](development/001-json-bundle-and-validation.md)。

## 目标绑定格式 v2

加载器继续接受 v1。使用 `schema_version: 2` 时，每个目标都必须显式提供 `binding`：没有绑定写 `null`，有绑定写下列对象。

```json
{
  "id": 10, "type": 1, "rule_version": 1, "target": 3,
  "binding": {"fact_type": 100, "subject": 1001, "input": "quantity"}
}
```

`fact_type` 与 `subject` 是 uint32 正整数；`input` 只能是 `"quantity"` 或 `"one"`。未知字段、缺失 binding、负数/浮点/字符串假 null 均拒绝。v1 目标不允许 binding 字段，避免旧格式静默承载新行为。包结构版本与 content_version、任务版本、规则版本分别独立。

绑定类型与对象是游戏接入方的编号约定，不自动检查物品外键，也不增加具体“拾取/交互”枚举。配置声明只决定路由；任意输入最终仍须目标处理器接受，例如标记目标使用 quantity 时，事实数量大于 1 会让整个投递失败。原奖励、价格跨表校验保持不变。

[bindings.json](../../../examples/configuration/bindings.json) 是 v2 样例，[game.json](../../../examples/configuration/game.json) 保留 v1 回归；`designer_config_check` 可校验两者。配置驱动事实投递、保存和恢复见 [示例](../../../examples/configuration/facts.cpp) 与 [开发步骤 002](development/002-objective-binding-schema.md)。

## 交互表格式 v3

schema_version=3 在原四表上增加必填 interactions 数组（可空），任务目标仍按 v2 显式提供 binding。行字段如下：

```json
{"id": 7, "version": 1, "enabled": true, "mode": "once", "fact_type": 200}
```

id/version/fact_type 为 uint32 正整数，enabled 必须 bool，mode 仅 once/repeatable。上限 4096 行，重复 ID、未知字段、缺失列或类型错误沿用首错诊断。v1/v2 不允许该字段，读取为空交互目录。Bundle.interactions() 返回只读领域目录，配置适配器增加对 designer::interaction 的依赖，领域库不反向依赖 JSON。

交互成功的事实 subject=点 ID、quantity=1；fact_type 的含义由游戏约定。任务还可能关注其他事实源，因此不把所有目标 subject 当作交互外键检查。已有点的生命周期不会因改 mode/版本重置，详见 [交互规则](../../../modules/gameplay/interaction/README.md)。完整 v3 样例见 [game.json](../../../examples/interaction/game.json)，取舍和验证见 [步骤 003](development/003-interaction-schema.md)。


## 独立对话和语言资源

本次增加 `designer::dialogue_configuration` 目标和 [dialogue_content.hpp](include/designer/configuration/dialogue_content.hpp)。它只依赖独立 dialogue，私有使用 persistence 和 JSON，不链接旧资产包、任务或商城；原 `designer::configuration` 与 v1/v2/v3 格式保持兼容。

| 接口 | 作用 |
| --- | --- |
| decode_dialogue_content / load_dialogue_content | 单份对话图的字节/文件入口，成功返回只读 DialogueContent |
| decode_dialogue_texts / load_dialogue_texts | 单份语言文本表入口，成功返回只读 TextTable |
| validate_dialogue_text_references | 发布/显示前检查图引用的全部正文键 |

图根字段全部必填：schema_version=1、content_version（uint64）、dialogue。dialogue 字段为 id、version、entry、completion_fact_type（uint32）、nodes。text 节点为 id/kind="text"/speaker/text_key/next；choice 为 id/kind="choice"/speaker/text_key/options，每选项 id/text_key/next；end 仅 id/kind="end"。示例见 [5001.json](../../../examples/dialogue/5001.json)。图 ID/节点 ID/选项 ID/speaker/版本和跳转均为正整数，适用图规则见 [对话模块](../../gameplay/dialogue/README.md)。

文本根字段 schema_version=1、content_version、locale、texts；texts 为键→UTF-8 正文的对象，示例见 [zh-CN.json](../../../examples/dialogue/zh-CN.json)。locale 为 1..32 字节的 ASCII 字母/数字/短横线标识，不宣称完整 BCP 47 校验；键 1..96 字节，仅字母/数字/点/下划线/短横线；正文 1..4096 UTF-8 字节。每文件最多 512 KiB、文本最多 4096 项、深度 32，沿用严格字段/类型/数字/重复键/UTF-8 和首错诊断。不支持语言回退、复数、格式化或语音资源。

每个入口只同步读取指定文件，不扫描所有资源。各自加载成功不代表图/文本引用匹配，发布前须调用 validate_dialogue_text_references；该接口只检查引用，不比较 content_version 批次。应用自行持有只读资源并安装 Catalog，错误不自动替换旧图。

`Catalog::replace_dialogue_definition` 支持校验后替换单图，进行中的会话保留旧图；Player 的 replace_dialogues 是调用方准备好的完整目录交换，不自动检查旧目录版本。正文的 pin/切换由 UI 决定；存档只存图和会话，正文旧键的跨版本保留需发布者负责。没有自动文件监听、异步缓存、manifest、远程下载、回滚或图/全部语言包原子发布。

Excel 适合编辑源、JSON 适合导出资源；推荐表结构与热更新演进见 [内容流程](../../../docs/content-pipeline.md)，实现选择及验证见 [步骤 004](development/004-dialogue-content-loading.md)。
