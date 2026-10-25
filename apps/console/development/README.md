# 命令行参考应用：开发记录

- [001：可交互应用层](001-playable-application.md)：场景、装配、保存与恢复、命令输入和端到端验证。

这是组合各系统的应用层，不是额外的玩法领域模块。遵循 [记录约定](../../../docs/development-records.md)。

- [002：应用拥有内容目录](002-own-content-catalogs.md)

## 注释与格式维护（2026-10-25）

核对本次学习注释和格式调整，忽略注释与空白后C++代码词法单元一致，未发现逻辑变更；清理行尾空白。VS Code构建目录索引排除设置同时保留。cmake --build build --parallel 重新构建通过，git diff --check通过；本次未新增或重跑业务测试。
