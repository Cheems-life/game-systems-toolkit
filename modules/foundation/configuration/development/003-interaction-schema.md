# 003：世界交互配置 v3

- 日期：2026-10-22；状态：已验证（实验性）。
- 关联代码：bundle.hpp、bundle.cpp、examples/interaction/game.json。
- 导航：[索引](README.md) · [交互设计](../../../gameplay/interaction/development/001-inspect-points.md)。

## 场景、目标与范围

JSON 配置检查点，交互成功的事实可由既有任务绑定消费。适配器依赖 interaction，领域库不反向依赖 JSON。

## 数据模型与规则

schema v3 在原四表上增加必填 interactions，可为空。行字段 id/version/fact_type 为 uint32 正整数，enabled 为 bool，mode 仅 once/repeatable。v3 目标沿用 v2 binding 契约。v1/v2 不接收 interactions 字段，加载为空交互目录。

通用 fact_type/subject 没有全局类型注册语义，不把全部目标 subject 强行当作交互点外键；目标也可关注别的事实源。其他结构、奖励引用与资源限制沿用前版，交互表最多 4096 行。

## 方案与选择理由

显式新版本比为旧格式静默加入表更明确。保留已有加载器和完整包发布，不引入通用反射框架。mode 为持久化生命周期语义，改配置不会迁移已完成记录。

## 实现过程与失败处理

先识别支持的 schema，再按版本检查根字段，逐行检查与领域目录构造，最后一次返回含交互目录的完整 Bundle。失败保留原包，不修改玩家。

## 验证

2026-10-22，macOS arm64。普通 Debug（GCC 15.2.0）完整 CTest 43/43 通过；AppleClang 17 的 ASan/UBSan 完整 CTest 43/43 通过，无报告。命令为 `cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure`，Sanitizer 目录为 build/sanitized。

本模块重点：v3 必填交互表及严格字段、布尔/模式/正整数/重复/上限诊断；v1/v2 空交互目录；v3 目标沿用 v2 绑定；文件加载后实际投递，以及持续分配失败时不发布半包。

完整验证明细见 [交互步骤 001](../../../gameplay/interaction/development/001-inspect-points.md)。本地文档链接校验通过；未验证其他平台、生产性能、引擎物理交互、并发、数据库或真实断电，不声称提供这些保证。

## 限制与下一步

无在线热更、编辑器或交互效果脚本。文件版本、配置内容版本、交互点规则版本各自独立，不能用改版本复位一次性状态。

## 决策演进

扩展步骤 002；v1/v2 读取保留。
