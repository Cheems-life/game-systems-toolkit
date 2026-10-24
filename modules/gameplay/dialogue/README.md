# 对话与叙事

状态：第一版实验性 C++17 实现，独立于引擎、任务、UI、JSON 和文件系统。提供流程与恢复规则，尚不满足完整商业叙事系统的验收范围。

## 先理解三个对象

| 对象 | 保存什么 | 谁负责 |
| --- | --- | --- |
| Definition / Catalog | 对话 ID、版本、入口、节点、文本键、完成事实类型 | 内容定义与当前目录 |
| Book / Session | 冻结流程、当前节点、修订号、状态、成功命令回执 | 玩家对话进度 |
| TextTable | 某语言的文本键 → 正文 | 独立配置适配器和 UI，核心不读取正文 |

正文改写不应该迫使业务状态保存整段文本；流程配置更新也不应该让进行中的会话突然跳到另一套节点。图与正文分离、会话固定开始时的图，分别解决这两个问题。Excel、JSON、按需加载与更新的完整说明见 [内容流程](../../../docs/content-pipeline.md)。

## 第一版规则

- text：speaker、text_key、一个 next；由 advance 命令前进一步。
- choice：speaker、text_key、有序 options；choose 只能选择配置中存在的选项 ID，显示顺序保留配置顺序。
- end：无正文、选项或后继。到达结束节点仍是 active，必须显式 finish；cancel 可在任意活动节点退出，状态为 cancelled。
- 每个 Book 同时最多一个活动会话。已完成和已取消的会话保留，支持历史请求重放。
- 节点 ID 在图内唯一，选项 ID 在节点内唯一；入口/跳转必须存在，所有节点必须从入口可达、且存在通往 end 的路径。允许有出口的手动循环，每条命令仅走一步，不自动执行循环。
- speaker 是游戏约定的角色 ID，不是引擎对象，也不要求绑定某个世界交互点。

选择不直接接任务、发奖或修改世界。完整 Player 在 finish 时生成 `type=冻结 completion_fact_type、subject=dialogue_id、quantity=1` 的通用事实，与对话状态在同一候选提交；任务是否完成由已有目标绑定决定。多个会话可以产生多次完成事实，并没有“每玩家只能完成此对话一次”的规则。

## 命令与失败

`Command` 的所有未使用字段必须为 0，所有身份字段/修订号按对应动作要求为正整数：

| 动作 | 必填字段（除 request_id/action） |
| --- | --- |
| start | dialogue |
| advance / finish / cancel | session、expected_node、expected_revision |
| choose | session、expected_node、expected_revision、option |

会话 ID 等于开始请求 ID，起始修订号为 1，每次成功操作增加 1。期望节点和修订号一起阻止旧 UI 点击：即使循环回到同一节点，旧修订号也不能再次生效。

成功请求先查原文重放，不因目录删除或会话结束失效；相同 ID 不同命令返回 request_conflict。失败命令不记录、不占用请求 ID。调用方负责稳定生成新请求 ID，重试沿用原命令；数据不能直接信任客户端输入。

返回 Result 包含领域错误、可选 Receipt、replayed；Player 的 DialogueResult 另有 quest_error。无效命令、过期视图、错误节点种类、未知选项、日志/会话上限等均不修改状态。分配失败向上传播，候选销毁，原状态保留。

独立 Book 的 finish 需要可信调用方提供非零、比本簿历史完成事件更大的 completion_event；其他动作传 0。Book 自身不投递任务，也不认证事实。需要对话/任务一致提交时使用 Player，不要先调用 Book 再单独发送事实后宣称原子完成。

## 接入示意

```cpp
#include <designer/dialogue/dialogue.hpp>
namespace d = designer::dialogue;
d::Catalog catalog({{5001, 1, 1, 400, {
    {1, d::NodeKind::text, 7, "npc.hello", 2},
    {2, d::NodeKind::end}
}}});
d::Book book;
auto started = book.execute_dialogue_command(catalog, {100, d::Action::start, 5001});
auto advanced = book.execute_dialogue_command(catalog, {101, d::Action::advance, 0, 100, 1, 1});
auto finished = book.execute_dialogue_command(catalog, {102, d::Action::finish, 0, 100, 2, 2}, 1);
```

CMake 独立规则与字节存档链接 `designer::dialogue`。配置加载另链接 `designer::dialogue_configuration`，完整玩家联动链接 `designer::player`，磁盘写入另加 `designer::persistence`。公共头分别为 [dialogue.hpp](include/designer/dialogue/dialogue.hpp)、[archive.hpp](include/designer/dialogue/archive.hpp)。

query_dialogue_session 返回借用的定义/节点引用；Book 或所属 Player 成功修改/恢复后视图可能失效，不跨调用缓存引用。跨操作保存值请用 snapshot。当前 Catalog 的共享定义仅供会话固定生命周期，不表示这些对象支持并发修改。

## 版本替换与恢复

Catalog::replace_dialogue_definition 校验后替换同 ID 图；相同规范化定义幂等，改内容必须提高图 version。删除当前图不破坏已开始的会话。完整目录替换允许调用方自行决定版本策略；版本号不是自动内容发布或防回滚机制。

Book::restore 从快照中的冻结图重新执行成功命令，逐条核对回执及最终会话，全部通过才交换。恢复不依赖当前目录，也不调用任务、场景、网络或发奖。图随存档保留，正文与 UI 语言不保存；新文本资源若删掉旧键，需要保留旧资源或显式处理缺失内容，不能仅靠图冻结保证显示。

独立格式 DDLGARCH v1 为小端，FNV-1a 校验仅用于完整性检查，不是认证。最大 4 MiB，编码/解码失败不覆盖输出。字段顺序：

1. magic 8 字节、version u32、session_count u32。
2. 每会话：id u64；图 id/version/entry/completion_fact_type u32、node_count u32；每节点 id/kind/speaker u32、key（u32 长度+字节）、next u32、option_count u32；每选项 id u32、key、next u32；最后会话 node/status u32、revision u64。
3. command_count u32；每回执：request_id u64、action/dialogue u32、session u64、expected_node u32、expected_revision u64、option u32；sequence/session u64、node/status u32、revision/completion_event u64。
4. 前述全部字节的 FNV-1a u64。枚举数值以公开头为准，未知值拒绝。

完整玩家格式写 DQSTARCH v4，兼容读 v1/v2/v3/v4；v4 在交互列表后嵌入此独立存档，旧格式补空对话状态。每条完成回执必须关联同 ID/类型/对象/数量的原始任务事实，且不能和交互共享事实归属。格式细节与跨模块边界见 [Player](../player/README.md)。

## 运行与验证

```sh
cmake -S . -B build
cmake --build build --parallel
./build/examples/designer_dialogue_example examples/dialogue build/dialogue-demo.sav
./build/examples/designer_dialogue_example examples/dialogue build/dialogue-demo.sav --load
ctest --test-dir build -R dialogue --output-on-failure
```

第一条示例加载所选图/语言，开始 v1 后发布 v2，旧会话前进到选项处并替换指定示例存档。下一进程用空当前任务/对话目录恢复，在旧图上选择并完成，通过事实推进任务、领奖、验证请求重放。示例文件写入仅支持已实现的 POSIX 适配，UNIX 构建才注册保存/加载 fixture。

## 上限和后续扩展

当前目录最多 128 图，每图最多 128 节点、每个选择最多 8 项，键最多 96 ASCII 字节。每玩家最多 16 个会话（包括已关闭）、默认 256/最多 1024 条成功命令；不淘汰，满后明确拒绝。这是有界学习原型，长期运营前需要设计历史归档与去重保留策略，不能直接静默删除记录。

每次命令复制有界历史并提交；恢复按完整日志重放，不承诺大型存档性能。图共享减少候选中图复制，但 snapshot/编码仍复制冻结图；多个同图会话目前重复存图。没有生产性能测量、多线程、数据库事务或多人竞争保证。

未实现：条件选项、变量、节点处理器注册、任意脚本、自动接任务/发奖、NPC 配置绑定、语音/演出、UI、完整本地化、跨版本会话迁移、Excel 导表器和远程更新。新增节点算法仍需代码，不假装配置可以定义任意业务逻辑。

- [开发步骤与验证](development/README.md)
- [内容编辑/加载/更新](../../../docs/content-pipeline.md)
- [工程质量要求](../../../docs/quality.md)

## 当前目录所有权

应用持有dialogue::Catalog，调用 `player.execute_dialogue_command(catalog, command)`；Player不再提供replace_dialogues。Book中的活动/历史会话继续持有冻结图，目录变化不影响它们，见[步骤002](development/002-external-player-catalog.md)。
