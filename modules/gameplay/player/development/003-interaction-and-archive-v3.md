# 003：交互、任务事实与完整存档 v3

- 日期：2026-10-22；状态：已验证（实验性）。
- 关联代码：player.hpp、player.cpp、archive.cpp。
- 导航：[索引](README.md) · [交互模型](../../interaction/development/001-inspect-points.md)。

## 场景、目标与范围

交互成功记录与任务推进必须一起发生，任务历史满等失败不能消耗一次性交互。接入独立 interaction 模块，不增加引擎调用、异步消息或物品发放。

## 数据模型与规则

Player State 增加 Book，外部当前交互目录独立保存。query_interaction 与执行使用相同业务条件；interact(Request) 查旧记录优先，再用 QuestBook.event_cursor()+1 分配事实 ID。cursor 为已成功事件和所有已接任务 accepted_after 的最大值，uint64 用尽时明确拒绝。新请求不能领用已有事实。交互和经济请求 ID 是不同域。

候选执行交互 → 候选 observe(type, point,1) → 准备结果 → 一次指针交换。失败丢弃全部候选。恢复校验每条交互凭证恰好链接同 ID/类型/对象/数量的原始任务事实，当前配置不参与历史解释；允许旧的非交互事实没有交互凭证。

DQSTARCH 写 v3，读 v1/v2/v3。v3 在 v2 领奖列表之后增加交互记录数 u32，每条为 request_id u64、point u32、version u32、mode u32、fact_type u32、event_id u64；仍 LE/FNV-1a/16 MiB。旧格式补空交互历史，重新保存升级；旧端不能读取 v3。

## 方案与选择理由

由协调器分配事件序号，比要求每次交互请求都由调用方选择序号更不易因重试改号。纳入接取边界避免新成功交互被当作接取前事实。保存凭证与事实关联，防止只恢复 completed 或只恢复进度。外部 observe/apply 保留为可信低层接口，不能据此认证引擎里的行为。

## 实现过程与失败处理

每个既有候选复制/购买/快照路径都保留交互簿。任务投递失败返回 interaction::fact_rejected 及嵌套任务原因；重放不交换，不重算当前配置。独立 Book 的提交仅发生在 Player 候选内；数据库、多线程及场景副作用不在此边界。

## 验证

2026-10-22，macOS arm64。普通 Debug（GCC 15.2.0）完整 CTest 43/43 通过；AppleClang 17 的 ASan/UBSan 完整 CTest 43/43 通过，无报告。命令为 `cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure`，Sanitizer 目录为 build/sanitized。

本模块重点：自动序号包含接取边界；任务拒绝保留交互簿/任务/资产；失败后同请求重试；既有购买/领奖保留交互状态；v3 固定样本、记录与事实关联及旧版读取；编解码/恢复逐分配点注入；独立进程空目录重放。

完整验证明细见 [交互步骤 001](../../interaction/development/001-inspect-points.md)。本地文档链接校验通过；未验证其他平台、生产性能、引擎物理交互、并发、数据库或真实断电，不声称提供这些保证。

## 限制与下一步

深复制成本未压测。当前只有无物品检查交互；需要宝箱时另行设计资产效果阶段。历史上限同时受任务日志限制；无自动压缩与幂等过期策略。

## 决策演进

扩展步骤 002 的候选事实提交，格式升级 v3，旧文档作为 v2 历史保留。
