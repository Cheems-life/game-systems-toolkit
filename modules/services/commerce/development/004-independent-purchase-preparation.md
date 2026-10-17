# 004：可复用的商城购买准备

- 状态：已验证（本机规则、格式及异常安全范围）。
- 日期：2026-10-18。
- 关联代码或提交：`feat: add generic quests and shared player settlement`。
- 导航：[本系统索引](README.md) · [任务步骤 004](../../../gameplay/quests/development/004-player-coordination-and-claim-plan.md)。

## 场景、目标与范围

原 Shop 独占 Economy，任务若另持有钱包会分叉；如果任务直接修改 Shop 或嵌套任意回调，又难以解释资产与任务状态一起提交。

## 数据模型与规则

新增 PurchaseBook，仅拥有购买历史及其上限。prepare 输入只读 ProductCatalog、只读 Economy 和购买意图，返回 PreparedPurchase：已准备回执、独立经济候选、独立购买记录候选。成功重放/失败没有需要提交的候选。

## 方案与选择理由

不采用全局可写钱包、向 Shop 塞任务逻辑或公开可写 State。提取纯准备边界可被独立 Shop 和新 Player 同时复用。也没有采用“传可写经济引用、执行后再分配记录”的弱异常接口；输入 const 使准备失败无法半修改真实资产。

## 实现过程与失败处理

保留商品校验、先查历史再查配置、计价和冲突语义。prepare clone 输入经济并复制记录，完成经济执行和两层回执准备，再返回拥有的候选。Shop 为其构造新 State 并交换；Player 增加任务状态后统一交换。PurchaseBook::restore 检查关联并准备记录 map，当前资产规则由所有者在相同候选验证。

## 验证

既有商城规则、格式、分配失败与独立进程测试均回归；新玩家测试确认商城消费和任务领奖在唯一余额上发生。独立 CMake 工程可链接 quests；纯核心消费者只链接 quest_core，不链接 commerce。

- 环境：macOS，普通 Debug 构建 GCC 15.2.0；完整回归 23/23 通过。包含最终事件边界检查。
- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`、`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例使用 `build/example-quests.sav` 保存，再加 `--load` 独立加载。
- 内存检查：AppleClang 17，`build/sanitized` 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`，链接对应 sanitizer，完整 23/23 通过。
- 外部集成：独立 CMake 项目分别仅链接 designer::quest_core 与 designer::quests + designer::persistence，构建运行通过，子项目默认关闭示例/测试。
- 本地文档链接及 Git 差异空白检查：通过。
- 未验证：并发、数据库、网络乱序重排、真实断电、跨平台与性能压测。

## 限制与下一步

保留独立 Shop 的源码使用入口和 DSHPARCH 格式，不承诺 ABI 稳定。准备深复制仍有成本。PurchaseBook 不提供独立账户服务，也不负责证明外部资产快照真实性。

## 决策演进

补充商城步骤 002 的候选机制并替换其私有历史存储方式，外部购买语义不变。新共享协调边界见任务步骤 004，旧记录保留。
