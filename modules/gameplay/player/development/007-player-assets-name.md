# 007：明确玩家资产成员命名

- 日期：2026-10-24；状态：已验证。
- 导航：[索引](README.md) · [详细架构图](../../../../docs/architecture-details.md)。

## 问题与选择

State::economy 容易被理解为全局经济服务，而其实际作用是独占当前玩家的钱包、容器与成功交易记录。改名为 assets，构造参数同名，类型仍使用 economy::Economy 表明模块来源。未改为 wallet，因为数据范围超过钱包。

## 实施与边界

更新 Player 初始化、空指针校验、克隆、购买、领奖、快照和恢复的内部访问。购买结果 PreparedPurchase.economy 是其他类型的已有字段，不连带改名；公开 economy() 查询保留，避免本次内部重命名扩大为调用方迁移。配置、快照字段、字节格式和失败语义均不变。所有权仍为唯一玩家资产对象，不新增副本或共享状态。

同步当前README与成员图，历史记录保留原名称。没有新业务规则，不新增重复测试，使用现有构建与回归验证。

## 验证

本机AppleClang 17，cmake --build build --parallel成功；ctest --test-dir build --parallel 4 --output-on-failure，50/50通过；git diff --check通过。没有并发、数据库或性能变化，不声称新增相关保证。
