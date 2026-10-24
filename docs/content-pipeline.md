# 内容编辑、导出、加载与更新

当前对话第一版先建立稳定的运行接口。Excel 导出工具、异步资源管理和远程内容发布尚未实现；本文把可用能力与后续设计分开。

## Excel 和 JSON 分别负责什么

内容多起来以后，用 Excel、Google Sheets 或专用工具编辑，经校验后导出运行资源，是可采用的成熟流程。Epic 的 [Data Driven Gameplay](https://dev.epicgames.com/documentation/en-us/unreal-engine/data-driven-gameplay-elements-in-unreal-engine) 官方示例使用 Excel 导出 CSV，再导入 DataTable；Unity Localization 的 [CSV 导入导出](https://docs.unity3d.com/Packages/com.unity.localization@1.5/manual/CSV.html) 也支持表格与文本表转换。这些是工具支持的实例，不表示所有团队都采用 Excel。

Excel 适合大量同结构的条目、数值、筛选、批量填充和翻译协作。长篇分支剧情还需要查看跳转关系与上下文，可能更适合剧情脚本或节点编辑器。CSV 是表格交换格式，JSON 是运行/交换格式；二者都可以从编辑源生成。Excel 本身不解决唯一 ID、外键、发布一致性和版本迁移。

建议后续采用单一编辑源，生成资源不再手动维护两套：

```text
Excel / 剧情工具
    ↓ 导出：显式字段类型、ID/引用/规则检查
按任务组、章节、地图、语言生成 JSON（必要时再考虑二进制）
    ↓ 完整校验，构造只读领域对象
按需发布与使用
```

JSON 长并不代表运行时一定慢，但手写嵌套结构和大文件合并冲突确实会增加维护成本。把结构改为表格编辑、运行资源拆分，比直接替换文件后缀更有价值。先测量解析时间、内存与加载频率，再决定是否需要二进制格式；本工程未做生产性能测量。

## 表格如何拆

下面是导表器未来可使用的列设计，当前未交付 xlsx/CSV 解析工具。列名对应已实现的对话模型，不要求把嵌套 JSON 塞进一个单元格。

| 编辑表 | 建议列 | 关系 |
| --- | --- | --- |
| dialogues | id, version, entry, completion_fact_type | 入口属于本对话 |
| nodes | dialogue_id, id, kind, speaker, text_key, next | dialogue_id + id 唯一；列按 kind 检查 |
| options | dialogue_id, node_id, id, display_order, text_key, next | 选项 ID 局部唯一，按显示顺序导出 |
| texts | key, zh-CN, en-US, translator_note | 导出时每种语言一份键值文件 |
| quests | id, version, minimum_level, policy, title | 沿用现有任务规则 |
| objectives | quest_id, id, type, rule_version, target, fact_type, subject, input | 关联任务、处理器和事实约定 |
| quest_rewards | quest_id, asset_kind, asset_id, amount | 一条奖励一行，导出时聚合 |

导出规则应拒绝重复 ID、缺失外键、未知列、错误枚举及越界数字，并提供表名/行/列诊断。可选字段的空值必须有明确语义：例如 text 节点没有 options，end 节点不需要 speaker/text_key。不要用空白偷偷代表 0 或合法默认值。

uint64 ID/数量不能经 Excel 浮点单元格往返，超过精确整数范围的内容应按文本列读取并严格解析十进制；本项目运行 JSON 接口保留完整 uint64，拒绝 `1.0`、科学计数法和数字字符串。导表器需要负责把编辑源的文本数字转换为正确的 JSON 整数字面量。公式应约定导出计算值还是禁止，不能假定运行端会计算；换行、引号、编码也应由正式序列化器处理。

新增同类任务、文本或图节点可只改数据。新增任务目标算法、对话条件/脚本、发奖方式仍需要代码与对应校验，配置不会自动获得新行为。

## 文件大时如何加载

不需要把所有正文塞进启动 game.json。当前基础规则包仍是一次完整加载，最大 4 MiB，没有借本次对话开发重写资产/任务包。

当前新增的接口按调用方指定文件同步加载：

- `load_dialogue_content(path)`：一份对话图，例 `examples/dialogue/5001.json`。
- `load_dialogue_texts(path)`：一份语言文本表，例 `examples/dialogue/zh-CN.json`；以后可以按章节/区域进一步分文件。
- `validate_dialogue_text_references(definition, texts)`：在发布或显示前检查所选文本表是否包含所有键。

只调用所需文件就只解析这些资源。每文件最多 512 KiB，文本表最多 4096 项、每条最多 4096 UTF-8 字节；深度最大 32。不是流式 JSON，也没有自动扫描文件目录、异步 IO、分片索引或缓存淘汰。内存中的解析树、只读结果、候选状态及旧版本可能同时存在，磁盘大小不能直接当作内存峰值。

以后地图/章节切换时，可由应用层通过“资源 ID → 文件/分包”的清单找到资源，后台读取并校验，在主线程发布；缓存要按预算与引用计数清理。活动会话持有旧图，不能在新版本发布时强制释放。启动时一次加载小而高频的基础定义也可以，拆分粒度由实际访问与测量决定。

## 热更新究竟更新什么

配置热更新只改变已有代码支持的数据。新增 C++ 算法、结构不兼容的字段、引擎资源引用变化，需要相应兼容方案和程序版本。可参考 Unity 官方 [Addressables Content Update Workflow](https://docs.unity.cn/Packages/com.unity.addressables%401.17/manual/ContentUpdateWorkflow.html) 对内容更新与程序更新的边界说明；本工程未接入 Addressables。

### 本版已经实现的进程内更新

1. 显式读取候选图和所选文本资源，检查格式、图规则和文本键引用。
2. `Catalog::replace_dialogue_definition` 安装一份图：相同图可幂等重装；已有 ID 改内容需提高图 version，失败保留原目录。
3. 应用持有当前 `dialogue::Catalog`，在自身配置容器中发布准备好的目录，并通过 `Player::execute_dialogue_command(catalog, command)` 只读传入。它是完整目录替换，不自动比较旧目录版本，也不会替调用方合并图；如果要保留其他图，应先复制旧目录，在副本上安装并整体发布。不要把这当作防回滚机制。
4. 新开始的会话读取新图；活动/历史会话持有开始时的不可变图。恢复直接使用存档中的旧图和命令，不依赖当前文件。

文本表独立于对话。希望一句话或整个会话始终显示同一版本正文，UI 应持有对应只读 TextTable；切换语言/文本资源时需先校验。只冻结图并不保证旧键永远存在于新文本表。存档没有正文、locale 或 UI 资源批次；跨版本重启时，发布者必须保留旧键/旧文本包，或者明确拒绝显示缺失内容，不能承诺任意删改文本都能继续展示。

活动会话默认继续旧流程，是为了避免一次点击读新版、下一次点击指向不存在的节点。图修复若必须影响正在进行的会话，需要新增明确迁移或取消重开策略，不自动把节点号套入新图。旧目录移除后重加同 ID 没有持久版本墓碑；是否允许降版由发布层决定。

### 后续线上发布才需要的能力

实际内容发布应有 manifest（批次、资源清单、hash、schema/程序兼容范围、依赖关系）、完整下载到暂存区、完整性与兼容校验、构建候选资源集合、一次切换有效集合、旧批次保留及失败回退。涉及不可信分发还应设计来源认证。图和所有语言表应以一致发布单元管理，不能把多次单资源 replace 称为整包事务。

当前未实现文件监听、CDN/网络下载、manifest、签名、异步发布、自动回滚、跨图/语言包一致切换，也没有数据库、多线程或多进程事务。单线程内存交换与磁盘存档是两个边界。

## 下一步建议

先用现有对话样例稳定 definitions/nodes/options/texts 的列，再做最小导表器：Excel 编辑 → 导出拆分 JSON → 复用现有校验器 → 错误定位表格单元格 → CI 校验。之后有第二个地图/语言和测量数据，再做索引缓存与远程发布，避免一次引入尚未验证的完整内容平台。
