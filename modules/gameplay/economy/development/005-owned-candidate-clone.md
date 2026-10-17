# 005：供商城协调的独立经济候选副本

- 状态：已验证（本机规则及异常安全范围）。
- 日期：2026-10-17。
- 关联代码或提交：本次商城第一版，提交主题 `feat: add versioned shop purchases and persistent replay`。
- 导航：[本系统索引](README.md) · [商城步骤 002](../../../services/commerce/development/002-retry-and-atomic-purchase.md)。

## 场景、目标与范围

商城在扣款发货之外还必须记录购买意图。若直接对真实 Economy 调 execute 后再分配商城记录，分配失败会造成资产已改而商城记录缺失。

## 数据模型与规则

增加 Economy::clone() const，返回 unique_ptr<Economy>。克隆包含自有钱包、所有容器/规则、成功历史及 history_limit。与原对象不共享可写状态，不改变原对象。既有 execute/restore、DECOARCH 版本及独立经济使用方式不变。

## 方案与选择理由

比较了先执行后补偿、借 snapshot/restore 复制、公开可写 State 和显式 clone。补偿存在失败和误重试风险；反复 restore 会执行存档规则验证；公开 State 破坏封装。因此选择 const clone 作为小范围能力，供具有所有权的协调器构造候选，不加入商城类型依赖。

## 实现过程与失败处理

1. 按原钱包与历史容量构造候选外壳。
2. 深复制内部 State 到候选。分配失败由 unique_ptr 清理，原对象不变。
3. 返回独立所有权。调用方可在其上执行，但不会发布为真实状态，除非外层协调器自行提交。

clone 的创建和使用没有改变原状态或使原视图失效；克隆内部成功修改仍遵守旧视图失效约定。

## 验证

商城规则测试检查副本初始快照一致、修改副本不影响原件。分配测试逐点注入 clone 失败，验证原资产与记录不变，并验证克隆保留历史上限。完整回归仍包含旧经济模块所有测试。

- 已执行：macOS，现有 Debug 构建（GCC 15.2.0）；构建后完整 CTest 17/17 通过，后续新增边界的商城 5 项复查通过。
- 命令：`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例 save/load 使用 `build/example-commerce.sav`。
- 内存检查：AppleClang 17 的 Debug 构建启用 AddressSanitizer/UndefinedBehaviorSanitizer，完整 CTest 17/17 通过。命令：`cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`，随后构建并执行 CTest。
- 外部集成：独立 CMake 工程通过 add_subdirectory 仅链接 designer::commerce 与 designer::persistence，默认不构建本工程示例和测试；AppleClang 构建运行通过。
- 文档检查：本地 Markdown 链接与 git diff --check 通过。
- 未覆盖：真实断电、并发、服务端数据库、平台支付及性能预算。

## 限制与下一步

目前会先创建外壳并复制钱包，再复制 State，且商城中经济 execute 还会复制候选，存在额外成本；未测量性能。没有为优化新增共享可写状态。未来若引入变更集，应保持原子性与所有权约定。

## 决策演进

补充经济步骤 002 的候选机制，商城步骤 002 使用它做更外层协调；不替代旧执行、去重和存档规则。

## 2026-10-17 维护补充

提交主题 `docs: preserve economy and shop learning annotations`：保存经济恢复流程及商城模型/流程的中文学习注释和排版整理。对三处源码忽略注释与空白后比较词法记号，结果与原提交一致；没有改变接口、数据格式或业务规则。执行 `cmake --build build --parallel` 与完整 CTest，17/17 通过。本次未重复运行内存检查。
