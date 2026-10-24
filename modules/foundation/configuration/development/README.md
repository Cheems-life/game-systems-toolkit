# 配置与数据定义：开发记录

状态：第一版实现，验证结果见步骤记录；仍属实验性。

[模块说明](../README.md) · [统一记录规范](../../../../docs/development-records.md)

- [001：JSON 配置包与跨表校验](001-json-bundle-and-validation.md)：启动加载、四表引用、严格诊断、处理器接入、配置驱动示例及失败验证。

- [002：目标绑定配置格式 v2](002-objective-binding-schema.md)

- [003：世界交互配置 v3](003-interaction-schema.md)

## 注释与格式维护

2026-10-24：检查并保留 bundle.cpp 与 json_reader.hpp 的学习注释和格式化修改，using 顺序调整不改变行为。配置示例和测试随玩家/任务 API 更名迁移；普通完整回归 43/43 通过。详细更名与其余验证见 [玩家步骤 004](../../../gameplay/player/development/004-explicit-business-api-names.md)。
