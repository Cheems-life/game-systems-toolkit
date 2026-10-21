# 世界交互

状态：实验性 C++17 第一版，支持每玩家检查交互点、一次/重复、成功请求重放与恢复。独立于 Godot、任务和经济。

## 先理解边界

```text
可信接入方确认身份、距离、视线和对象有效性
    ↓ Request{request_id, point}
交互 Book：启用状态、一次/重复、请求去重、成功记录
    ↓ 固定事实效果：type=配置，subject=交互点 ID，quantity=1
Player 候选中推进任务 → 全部成功后一次提交
```

第一版适用于阅读告示牌、检查遗迹等无物品效果交互。它不是交互点发现/碰撞系统，不处理宝箱发奖、对象全服共享状态、多人抢占、动画、输入或场景回调。上图第一步仍由可信游戏逻辑负责，不能直接把客户端的“我已靠近”视为事实。

## 类型与数据模型

| 类型 | 用途 |
| --- | --- |
| `Definition` | id、version、enabled、mode、fact_type |
| `Catalog` | 当前交互点定义；拒绝零 ID/版本/类型、重复 ID、非法模式和超过 4096 点 |
| `Mode::once / repeatable` | 每玩家一次 / 每个新请求可重复；不是每场景实例或每天一次 |
| `Request` | request_id、point，两者均为正数 |
| `Receipt` | 成功请求及冻结版本、模式、事实类型、事件序号 |
| `Book` | 一个玩家的成功记录、导出的每点模式索引和历史容量 |
| `View` | allowed 和原因；本地业务提示，不是成功承诺 |
| `Result` | error、可选凭证、replayed |
| `Snapshot` | 严格按事件 ID 排序的全部成功记录；不另存 completed 布尔 |

公共入口：[interaction.hpp](include/designer/interaction/interaction.hpp)，目标为 `designer::interaction`，无其他项目库依赖。

## 必须记住的规则

- 成功请求相同 ID、相同 point 重放，返回原凭证，不再次执行；复用 ID 请求另一点冲突。重放在当前目录、启用状态、容量和传入新序号校验之前，因此删表/停用不破坏成功重试。
- 一次点可以在没有任务时完成；之后接取任务不补算历史事实，换请求也无法重做。需要“历史上已完成检查”的目标时，必须另行设计持有状态/世界状态查询，当前只处理接取后的事件。
- once 的成功历史会阻止换请求 ID 再次完成；改版本或把当前配置改成 repeatable 也不重新开放。重复点成功后改成 once 报 configuration_conflict。需要不同生命周期时用新点 ID，正式迁移另行设计。
- repeatable 的每个新请求可以使用新版本/新事实类型，但历史结果保持冻结。发布者负责版本含义，不检查同版本不同内容。
- 独立 Book.execute 要求新的 event_id 为正且大于本簿已有成功记录，允许间隔。Player 集成时由协调器按共享事实流生成，不使用请求 ID 充当事实 ID。
- 成功记录默认最多 256、可配置 1..4096；不淘汰，满后拒绝新请求，旧重放仍可用。Player 还受任务事件历史容量限制。
- query 顺序为非法点、历史 once 完成、当前点存在/启用、模式兼容、交互历史容量。execute 复用同一组规则，但实际任务投递仍可能失败，瞬时提示不预执行目标处理器。
- restore 不依赖当前目录；检查请求唯一、版本/类型/模式合法、事件严格递增、同点模式一致、once 仅一条。全部通过再交换，错误和分配异常都保留原状态。

## 独立使用

```cpp
#include <designer/interaction/interaction.hpp>
namespace i = designer::interaction;
i::Catalog catalog({{7, 1, true, i::Mode::once, 200}});
i::Book book;
auto hint = book.query(catalog, 7);
auto result = book.execute(catalog, {100, 7}, 1); // 1 由可信序号源提供
// result.receipt 描述固定效果。独立 Book 成功不代表任务已推进。
```

Book::receipt 返回的指针在成功 execute/restore 后失效；跨操作保留数据使用返回的凭证副本或 snapshot。Player 的只读视图也会在成功状态提交后失效。

Book 使用局部候选记录/索引，准备返回结果后 swap；只保证单线程内存一致性。不要先对正式 Book.execute，再对正式任务 observe，然后声称两者能一起回滚。

## Player 与配置接入

```cpp
// Player 构造参数在既有 event_limit/purchase_limit 后追加交互目录和容量。
// 旧调用方式默认使用空交互目录，不需要修改。
Player player(products, quests, registry, std::move(economy), 256, 256, interactions, 256);
auto view = player.query_interaction(7);
auto result = player.interact({100, 7});
```

以上 Player 位于 designer::player，完整代码见 [示例](../../../examples/interaction/main.cpp)。交互请求 ID 和经济请求 ID 是独立域；交互生成的事实与外部 observe/apply 共用事件序号流。

Player 在候选内记录交互并投递事实，任务拒绝、历史满、65 项匹配超限或 bad_alloc 都不提交交互及任务。失败不占用请求或事件 ID；历史发生有效变化后可重试。交互本身不修改资产，任务领奖仍需显式调用。

query_interaction 另外检查共享序号溢出；它不保证任务日志有空间或所有目标处理器会接受输入。interact 的 fact_rejected 携带 quest_error，调用方据此决定重试。不要给同一次外部操作换新请求 ID。

JSON v3 增加必填 interactions 表：

```json
{"id": 7, "version": 1, "enabled": true, "mode": "once", "fact_type": 200}
```

type=200 的含义由示例约定为检查成功，不是任务核心的硬编码枚举。配置 v1/v2 读为空交互目录；玩家格式写 v3、读 v1/v2/v3，历史凭证和事实一同保存。删除交互目录后仍能恢复与重放。格式细节见 [玩家模块](../player/README.md)。

## 运行与限制

```sh
./build/examples/designer_config_check examples/interaction/game.json
./build/examples/designer_interaction_example examples/interaction/game.json build/interaction-demo.sav
./build/examples/designer_interaction_example examples/interaction/game.json build/interaction-demo.sav --load
ctest --test-dir build -R interaction --output-on-failure
```

首条保存命令会替换指定示例文件。示例先用 1 条任务日志预算触发失败，再在更大预算的 Player 中恢复原历史并以同请求重试；不是清除记录后冒充重试。

目前每次执行复制本簿记录，Player 还复制既有候选资产/任务；成本未压测。没有数据库事务、多线程竞争裁决、引擎适配、持久化确认、防回滚或身份认证。校验和不是签名；删除交互归属记录但保留事实可能被当作合法外部事实，不能以此证明文件未被修改。历史满时无自动归档或清理方案。

设计原因和实际验证见 [开发步骤 001](development/001-inspect-points.md) 与 [开发索引](development/README.md)。
