# 004：共享玩家经济与领奖准备计划

- 状态：已验证（本机规则、格式及异常安全范围）。
- 日期：2026-10-18。
- 关联代码或提交：`feat: add generic quests and shared player settlement`。
- 导航：[本系统索引](README.md) · [商城步骤 004](../../../services/commerce/development/004-independent-purchase-preparation.md)。

## 场景、目标与范围

商城先花钱，任务再发奖，必须使用同一钱包；任务已领奖是业务事实，不能靠更换经济请求 ID 再发一次。世界状态未来也需要进入共同候选，而非产生外部副作用。

## 数据模型与规则

Player 的唯一 State 为 Economy 所有权、PurchaseBook 和 QuestBook。商品目录另持有；任务目录由任务册管理。购买使用 PurchaseBook 返回的经济/购买候选，再复制任务册统一提交。领奖 ClaimRequest 为请求 ID、任务 ID、目标容器，成功 ClaimReceipt 关联原经济回执。

## 方案与选择理由

比较了给任务再持有一个 Economy、让任务持有 Shop 的可写别名、嵌套更多可写所有者，以及顶层玩家聚合。前者可能钱包分叉，别名与嵌套会使边界不清。采用顶层聚合，并提取无资产 PurchaseBook；保留旧 Shop 的独立场景。

领奖以固定类型的准备函数列表组织，不先引入事务基类或任意回调注册。将来可加入同一候选中的世界状态字段和准备步骤，最后仍只有一个提交点。

## 实现过程与失败处理

1. 查领奖历史，同意图返回原结果，不同意图冲突。
2. 查任务业务状态：未接/未达标/已领奖分别拒绝，换新请求 ID 也不能绕过。
3. clone 同一个玩家候选。prepare_rewards 调经济发全部冻结奖励；既有无对应领奖记录的经济 ID 不能被收编。
4. prepare_claimed 仅修改候选 claimed；prepare_receipt 准备候选回执。
5. 准备最终返回值，static_assert 要求无异常移动；最后一次状态指针交换。

资产、任务与历史各阶段的错误或 bad_alloc 均丢弃整个候选。没有场景、数据库或网络操作；文件保存仍在内存提交之外。

## 验证

player_tests.cpp 先购买后领奖，检查同一余额与背包；奖励超上限后通过购买消费腾出余额再重试同领取 ID；容量失败保持可领奖，选择另一个容器后成功。覆盖经济/购买 ID 冲突、降级、冻结奖励、换 ID 不重复领取及混合状态恢复。allocation_tests.cpp 对已有购买/任务的候选逐点失败，包括标记 claimed 之后准备记录和返回结果的分配阶段。

- 环境：macOS，普通 Debug 构建 GCC 15.2.0；完整回归 23/23 通过。包含最终事件边界检查。
- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`、`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例使用 `build/example-quests.sav` 保存，再加 `--load` 独立加载。
- 内存检查：AppleClang 17，`build/sanitized` 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`，链接对应 sanitizer，完整 23/23 通过。
- 外部集成：独立 CMake 项目分别仅链接 designer::quest_core 与 designer::quests + designer::persistence，构建运行通过，子项目默认关闭示例/测试。
- 本地文档链接及 Git 差异空白检查：通过。
- 未验证：并发、数据库、网络乱序重排、真实断电、跨平台与性能压测。

## 限制与下一步

当前无世界字段和世界修改实现；没有额外网络/数据库阶段。Player 成功修改后旧只读视图失效，所有访问单线程。复制含历史与配置，成本未测量。并非通用账号服务或分布式事务。

## 决策演进

替代需求步骤 001 中未决的共同协调结构。商城步骤 004 记录纯购买准备边界；独立经济/商城 API 与旧存档继续使用。
