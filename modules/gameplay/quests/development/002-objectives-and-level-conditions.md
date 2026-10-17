# 002：目标处理器与等级条件

- 状态：已验证（本机规则、格式及异常安全范围）。
- 日期：2026-10-18。
- 关联代码或提交：`feat: add generic quests and shared player settlement`。
- 导航：[本系统索引](README.md) · [商城步骤 004](../../../services/commerce/development/004-independent-purchase-preparation.md)。

## 场景、目标与范围

任务不能依赖怪物、NPC 等业务对象，又需要为后续目标行为扩展保留接口。当前开启条件为最低等级，表现层需要区分等级不足与上下文错误。

## 数据模型与规则

quests.hpp 提供 ObjectiveDefinition{id,type,rule_version,target}、ObjectiveHandler、ObjectiveRegistry、ConditionSpec、PlayerReadContext 和 ConditionResult。数字进度使用 uint64，范围为 0..target，单调不减。默认计数 type=1 与标记 type=2 都使用版本 1。组合策略单独表达，当前只接受 all。条件结果含 allowed、稳定 reason、独立 error 与 required/actual。

## 方案与选择理由

比较了主流程按玩法类型 switch、只声明两个枚举、处理器注册。前两者会把玩法或未来目标行为扩展挤进主流程，采用注册的纯处理器；但不把它包装成任意插件系统。数据明确限于有上限的数字进度，未来复杂载体需版本升级。任意达成/分阶段属于组合与激活边界，不由叶子处理器自动解决。

等级从可信只读上下文输入，不引入尚未实现的成长模块或第二份等级。evaluate 为纯函数，查询和实际接取均调用它；接取后的任务不再以等级阻止推进。

## 实现过程与失败处理

1. QuestCatalog 排序并检查结构、正数、重复 ID 与资源上限。
2. QuestBook 构造/替换目录时要求每个目标都有已注册且兼容的处理器。注册表被任务册以值接管，处理器共享为 const，装配后不暴露重绑。
3. update 在局部 next 上计算，校验有效、单调及不超过 target，再写候选进度。
4. complete 单独聚合所有叶子达成结果，不让状态流转理解具体目标类型。
5. 无效配置抛 invalid_argument，恢复未知类型/版本拒绝；不丢弃目标。

自定义处理器必须纯、确定性、noexcept；内部不可执行 I/O 或修改其他玩家。

## 验证

tests/quests/core_tests.cpp 检查等级低于/等于/高于门槛、无效上下文、查询后接取重新判断、降级后任务继续、重复/未知类型与版本，及自定义双倍计数处理器。纯核心测试链接 designer::quest_core，不链接 commerce。

- 环境：macOS，普通 Debug 构建 GCC 15.2.0；完整回归 23/23 通过。包含最终事件边界检查。
- 命令：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`、`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例使用 `build/example-quests.sav` 保存，再加 `--load` 独立加载。
- 内存检查：AppleClang 17，`build/sanitized` 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`，链接对应 sanitizer，完整 23/23 通过。
- 外部集成：独立 CMake 项目分别仅链接 designer::quest_core 与 designer::quests + designer::persistence，构建运行通过，子项目默认关闭示例/测试。
- 本地文档链接及 Git 差异空白检查：通过。
- 未验证：并发、数据库、网络乱序重排、真实断电、跨平台与性能压测。

## 限制与下一步

仅数字状态、两种默认处理器和 all 组合。扩展规则必须保留旧规则版本语义，否则旧存档恢复可能失败。复杂状态、条件组合、世界标记和任务树尚未实现。

## 决策演进

落实需求步骤 001 的注册/条件边界，不修改其中的等级和无玩法耦合方向。
