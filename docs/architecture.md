# 架构与学习约定

## 已确定的方向

- 学习目标：系统设计优先，C++ 实现用于检验设计。
- 参考场景：类 MMO；模块仍须能按需用于单机或其他玩法。
- 实现技术：C++17、CMake；Godot 是未来的引擎适配目标。
- 当前交付：本地构建骨架、静态库、命令行示例与 CTest 入口。没有完整运行时或联网服务。

## 目录与依赖

```text
modules/<分类>/<模块>/
  README.md                    职责、规则与验收要求
  CMakeLists.txt               实现后才注册构建目标
  include/designer/<模块>/     公开头文件
  src/                         私有实现
examples/<示例>/               演示模块使用
tests/                         验证关键规则
adapters/                      引擎及外部系统适配
cmake/                         构建辅助
docs/                          设计记录
```

每个已实现模块有独立构建目标和 `designer::<模块>` 别名。当前提供 `designer::core`、`designer::inventory`、`designer::persistence`、`designer::economy` 、`designer::commerce`、`designer::quests` 、`designer::interaction`、`designer::dialogue`、`designer::configuration`、`designer::dialogue_configuration` 和 `designer::player`；背包有独立实现，不依赖 core。其他模块在真正开发时再补代码目录。

模块只声明实际需要的依赖，不强制全部依赖 core。示例、测试与适配器依赖领域模块，领域模块不依赖它们。不要为了目录完整引入事件总线、服务定位器、万能基类或全局单例。

## C++ 约定

优先值类型、RAII 和显式所有权。需要多态或共享生命周期时再引入相应机制。公开头文件保持最小依赖，不使用 `using namespace`。命名空间采用 `designer::<模块>`。

接口设计时明确错误、原子性、边界及线程约束；暂不强制统一错误基类、异常策略或序列化格式。未注明并发保证的模块不应假设线程安全。

库版本、数据版本、配置版本及协议版本分别管理。当前版本仅表示工程骨架，不承诺稳定 ABI，也未提供安装包导出。

## 每个系统的设计步骤

1. 写具体使用场景和范围。
2. 写必须成立的规则，例如数量守恒。
3. 建立数据模型、状态和所有权。
4. 定义操作及失败时的状态变化。
5. 写最小实现、示例和规则验证。
6. 通过第二个使用场景评估复用，再考虑抽象。

MMO 的可信端、重复请求与持久化要求从设计阶段记录；可以先在本地验证领域规则，不要求立刻搭服务器。引擎适配与网络部署不能取代规则验证。

存档分层：inventory 负责快照、格式及合法性恢复，persistence 负责字节文件。两者互不链接，示例组合它们。相关容器统一保存与恢复；内存操作和磁盘保存仍属于不同提交边界。

经济模块依赖 inventory，由 Economy 拥有钱包、容器和去重历史，候选状态通过无异常指针交换提交。经济编解码器将余额、容器和成功回执统一保存；文件适配器仍不理解领域数据。聚合只读视图会在成功修改后失效，跨操作需使用快照。

商城依赖经济模块，通过所有权和候选 clone 协调资产与购买意图一起提交。商城重试在查询当前商品前执行，历史回执不受改价/下架影响；商城存档嵌入经济存档并验证回执关联。没有真实支付实现。

任务核心 `designer::quests`（别名 `designer::quest_core`）仅包含处理器、条件与任务簿，不链接商城或玩家模块。独立 `designer::player` 组合任务和商城，拥有唯一 Economy、无资产 PurchaseBook、QuestBook、interaction::Book 和 dialogue::Book，负责整体快照与存档。底层只读取必要上下文，不包含完整 Player；任务保留具体协调器友元前置声明而无反向链接。详见 [玩家模块](../modules/gameplay/player/README.md)。

玩家准备奖励、领奖标记和回执后一次提交，没有场景/数据库/网络副作用。原 Shop 可独立使用，内部复用 PurchaseBook::prepare；不另创建与 Player 自动同步的钱包。提取时完整存档沿用 DQSTARCH v1；之后的事实绑定/交互步骤已升级格式，当前写 v4 并兼容读 v1/v2/v3/v4。

配置适配器 `designer::configuration` 依赖当前四种领域目录，私有使用 JSON 与文件读取；领域模块不反向依赖它。加载器先构造完整校验包，再交给应用层创建对象。它处于配置接入层，不因为目录在 foundation 就成为所有领域的底层依赖；本步不改变 Player 内存事务、冻结规则或存档格式。详见 [配置说明](../modules/foundation/configuration/README.md)。

事实路由属于任务接入边界，QuestBook::observe_gameplay_fact 接收通用类型/对象/数量，不定义具体玩法枚举。绑定放在冻结目标中，原文随事件记录；直接更新和事实共用同一顺序流。Player 只在候选内转发，并为绑定任务领奖记录序号关闭边界。恢复据此重算历史匹配集合；没有把外部发物品和事实投递合成资产事务，相关重试策略仍由可信业务源负责。

世界交互 designer::interaction 是独立无项目库依赖的规则模块。Book 保存每玩家一次/重复模式与成功请求，不读取完整 Player、不调用任务或引擎。Player 组合交互簿与任务候选，分配大于已知接取/事件边界的事实序号，再一起提交。check_interaction_availability 是本地业务提示，任务投递仍可能失败。该步骤引入 v3，当前格式写 v4、读 v1/v2/v3/v4；没有跨玩家全局世界状态或多人竞争裁决。


对话 designer::dialogue 只依赖标准库，保存流程图、冻结会话与命令回执，不加载正文或调用任务。Player 在候选内完成会话并投递通用完成事实；任务只匹配绑定，不认识对话节点。独立字节存档 DDLGARCH v1 嵌入完整玩家 v4，旧版本补空对话。活动会话使用旧图，当前目录更新影响新会话。

对话配置 designer::dialogue_configuration 独立于旧资产包，只把单份图/语言 JSON 校验成只读对象，应用决定加载/发布。图与正文资源生命周期不同，不能把图冻结解释成全套本地化/资源热更新。编辑源、分包与发布的边界见 [内容流程](content-pipeline.md)。

[架构分析](architecture-review.md) · [成员与函数交互图](architecture-details.md)。

商品目录现由可信应用持有，Player::purchase_product(products, request)只读借用，不再持有products_或提供replace_products。交互与对话目录现也由应用持有，Player调用期借用，State中的两个Book保存各自运行记录，见[玩家步骤006](../modules/gameplay/player/development/006-external-product-catalog.md)。

## 可运行应用层

`apps/console`提供正式命令行入口designer_game，负责内容装配、命令输入/输出、请求ID和文件保存，领域状态仍由Player维护。见[应用说明](../apps/console/README.md)。这补充统一应用入口，不替代各模块或Godot适配。

交互/对话目录外置后的构造与函数签名迁移见[玩家步骤008](../modules/gameplay/player/development/008-external-interaction-dialogue-catalogs.md)，旧会话冻结与v4存档保持不变。
