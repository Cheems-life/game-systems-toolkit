# 005：玩家任务与商城统一存档

- 状态：已验证（本机规则、格式及异常安全范围）。
- 日期：2026-10-18。
- 关联代码或提交：`feat: add generic quests and shared player settlement`。
- 导航：[本系统索引](README.md) · [商城步骤 004](../../../services/commerce/development/004-independent-purchase-preparation.md)。

## 场景、目标与范围

重启后保留任务进度、冻结定义、事件去重、一次性领奖，以及共享的钱包/购买历史；当前目录移除任务或商品不能让旧请求重放失效。

## 数据模型与规则

PlayerSnapshot 嵌入 commerce::Snapshot（其内唯一经济快照）与 BookSnapshot。DQSTARCH v1 固定小端、16 MiB、FNV-1a。冻结规则与进度、事件完整编码；领奖通过请求 ID/经济序号关联经济记录，交易正文不重复保存。

## 方案与选择理由

分文件保存会出现资产与 claimed 状态错位，采用整体快照。未淘汰的事件可重建进度，提供比只检查进度范围更强的内部一致性检查，但不证明外部事实真实。

结构解码不应执行自定义处理器，也不能仅识别两种内置类型，否则扩展格式被解析器锁死。因此解析接受合法数字类型标识，最终恢复再用注册处理器校验并重建；未知类型/版本不是默默跳过。

## 实现过程与失败处理

1. 编码校验结构、各层回执关联与边界，嵌入旧商城字节；所有分配在输出交换前。
2. 解码先限制长度、魔数、版本、校验；读入受限集合，按 ID/序号找经济回执并检查无尾部垃圾。
3. Player::restore clone 当前玩家，在候选恢复当前经济规则、购买记录、任务冻结规则和重建进度。
4. 检查 claimed 与领奖记录对应、冻结奖励等于实际奖励、无商城/领奖 ID 重用。
5. 所有检查和分配通过后一次提交；失败保留当前玩家。

旧格式均不改版本；旧 Shop 快照可显式配空任务册导入，不凭空补任务或领奖意图。

## 验证

archive_tests.cpp 有独立 Python struct 构造并固定写入源码的小端 v1 样本，检查嵌套格式和目标进度；覆盖每个截断前缀、关联损坏、未来版本、2000 次字节变异。解析允许未知类型但恢复拒绝、删除事件但保留进度被重建校验拒绝。allocation_tests.cpp 检查编码/解码/恢复失败保留输出；独立 save/load 进程用空目录恢复事件/购买/领奖重放。

- 环境：macOS，普通 Debug 构建 GCC 15.2.0；完整回归 23/23 通过。包含最终事件边界检查。
- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`、`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例使用 `build/example-quests.sav` 保存，再加 `--load` 独立加载。
- 内存检查：AppleClang 17，`build/sanitized` 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`，链接对应 sanitizer，完整 23/23 通过。
- 外部集成：独立 CMake 项目分别仅链接 designer::quest_core 与 designer::quests + designer::persistence，构建运行通过，子项目默认关闭示例/测试。
- 本地文档链接及 Git 差异空白检查：通过。
- 未验证：并发、数据库、网络乱序重排、真实断电、跨平台与性能压测。

## 限制与下一步

没有格式迁移器、签名、防回滚、外部事实认证或真实断电测试。完整历史有容量限制；大规模持久化与查询需后续设计。新旧配置的资产规则不兼容时恢复失败，需要实际迁移策略。

## 决策演进

落实步骤 001 的统一存档目标，并细化结构解析与领域恢复分层；不改写旧格式历史。
