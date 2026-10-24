# 004：独立对话资源、文本键与本地更新边界

- 日期：2026-10-24；状态：已验证。
- 代码：dialogue_content.hpp、dialogue_content.cpp、diagnostic.hpp、src/json_validation.hpp。
- 导航：[索引](README.md) · [对话设计](../../../gameplay/dialogue/development/001-dialogue-flow-and-frozen-sessions.md)。

## 场景、目标与范围

大量文本/剧情不再继续塞入原基础规则整包。按单份对话和单份语言文本显式加载，复用严格 JSON 语法/诊断。新增 designer::dialogue_configuration，只依赖 dialogue 和私有 persistence/JSON，不带入商城/任务。

## 数据模型与规则

对话文件 schema_version=1、content_version、dialogue 对象；正文文件独立 schema_version=1、content_version、locale、texts 键值表。每文件最多 512 KiB，JSON 深度沿用 32。图使用稳定 text_key，正文按 locale 分包；检查引用时显式提供匹配文本表。文本键 ASCII 1..96 字节，正文 1..4096 UTF-8 字节，每表最多 4096 项。默认不提供缺失键回退文本来掩盖配置错误。

加载器同步读取指定文件，不扫描/加载所有目录。应用持有只读结果，自行决定载入和释放；Catalog 安装新图只影响后续开始的会话，活动会话持有旧图。文本资源的 pin/切换由 UI 持有资源决定，不宣称实现完整本地化框架。

## 方案与选择理由

编辑源与运行格式分开：Excel/CSV 便于批量编排，导出为校验后的 JSON/二进制；复杂图可用剧情工具。表格可拆 definitions/nodes/options/texts，用 ID 关联，避免单元格嵌套 JSON。当前先实现独立导入边界，不新增 xlsx 依赖或导表器。

Epic 官方 Data Driven Gameplay 文档支持 Excel 导出 CSV 再导入 DataTable；Unity Localization 支持 CSV/Google Sheets。它们证明此流程可行，不能推断所有团队均采用同一工具。来源见 docs/content-pipeline.md。

## 实现过程与失败处理

提取既有通用 Diagnostic、SAX 树和字段检查，不改变旧整包格式。新图/文本各自局部读取、严格检查并返回只读结果；显示前校验图中所有文本引用。完整读取和校验失败不发布新图。单图 replace_dialogue_definition 会校验相对当前图的版本；Player 的完整目录交换不自动比较旧目录，调用方需先准备候选并自行管理发布策略。流程冻结，不迁移正在进行的会话。

## 验证

环境和完整命令见 [对话步骤验证](../../../gameplay/dialogue/development/001-dialogue-flow-and-frozen-sessions.md#验证)。普通 GCC 全工程 50/50、AppleClang ASan/UBSan 50/50 通过；格式整理后的普通对话 7/7 通过。

`tests/dialogue/configuration_tests.cpp` 独立只链接 dialogue_configuration，验证分文件图/语言读取、文本引用及路径诊断、缺字段/未知列/重复键、错误枚举、空值/浮点整数、UTF-8、文本/文件长度、缺失文件、完整 uint64 内容版本、新版本安装与旧会话冻结、错误候选不发布。`allocation_tests.cpp` 逐点持续分配失败覆盖成功图/正文和失败诊断路径。旧整包 v1/v2/v3、绑定/交互表及配置异常测试完整回归通过。外部 CMake 消费者仅链接 dialogue / dialogue_configuration 构建运行成功，确认无 Player/任务/商城和公开 JSON 头依赖。修改 Markdown 的本地链接检查与 git diff --check 通过。

未验证：导表器（未实现）、全语言文本资源兼容、异步/跨资源发布、性能与非 macOS 平台。

## 限制与下一步

不含 Excel 导出、文件监听、异步 IO、自动缓存淘汰、manifest/CDN、签名或跨图/语言资源整体发布。线上更新需清单、完整性/兼容检查、失败回退；本步只验证已校验图的进程内替换。JSON 长度、磁盘大小与内存峰值是不同指标，尚未做生产测量。

## 决策演进

旧整包 v1..v3 保留；新剧情采用独立文件，不升级原整包 schema。文本与图分离是新增决定，不改变旧任务冻结规则。
