# 002：目标绑定配置格式 v2

- 日期：2026-10-21；状态：已验证（实验性）。
- 关联代码：src/bundle.cpp、src/json_reader.hpp、examples/configuration/bindings.json。
- 导航：[配置索引](README.md) · [任务步骤 007](../../../gameplay/quests/development/007-fact-bindings.md)。

## 场景、目标与范围

在目标配置中描述事实过滤和输入方式，玩法提交事实时不需要知道任务/目标 ID。复用完整包加载与诊断，不实现脚本、通配或事件类型注册中心。

## 数据模型与规则

继续读取 schema_version=1（无绑定）；新增 v2，每个目标必须有 binding 字段，可以为 null 或 {fact_type:uint32,subject:uint32,input:"quantity"|"one"}。整数保持正数规则；未知字段、未知 input、零 ID、假 null 均拒绝。事件类型及 subject 是游戏约定的编号，不自动解释为物品外键。

## 方案与选择理由

显式 v2 比静默给 v1 添加字段更清晰。绑定放在目标定义里自然随接取冻结。原 JSON 内部树曾用相同占位表示 null 和非 unsigned 数字；引入绑定 null 后需区别二者，避免 -1/1.5 被当作“无绑定”。

## 实现过程与失败处理

解析器保留 null/非法整数类型区别；按 schema 选择目标字段集合；检查绑定类型/字段/范围/模式，再用领域 Catalog 校验。失败沿用首错诊断和不发布半包。样例使用新格式，旧 game.json 保留 v1 回归。

## 验证

本次覆盖覆盖旧格式兼容、新格式全部字段、null 与错误数值区分、字段路径、处理器扩展，以及文件驱动投递/保存/恢复。


实际验证（2026-10-21，macOS arm64）：Homebrew GCC 15.2.0 Debug 完整 CTest 35/35 通过；AppleClang 17 的 ASan/UBSan 完整 CTest 35/35 通过，无报告。命令为 `cmake --build build --parallel 4` / `ctest --test-dir build --output-on-failure`，Sanitizer 目录为 build/sanitized。

本模块重点：v1 保持原字段；v2 的 null/对象、必填字段、未知列、模式、正整数/溢出及非整数不能冒充 null；加载后路由、v2 分配注入与文件校验命令。

完整跨模块验证明细见 [任务记录](../../../gameplay/quests/development/007-fact-bindings.md)。本地文档链接校验通过；未验证其他平台、生产性能、并发、网络乱序或真正断电，不作数据库/外部世界事务保证。

## 限制与下一步

不校验玩法事件编号是否存在或 subject 是否为某类物品，不推断“获得”“当前持有”“击杀”的语义。绑定匹配结果不等于事实经过认证。

## 决策演进

扩展步骤 001 的单一 v1 读取策略；其余四表与资源上限不变。
