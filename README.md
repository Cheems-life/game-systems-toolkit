# 游戏通用模块工具包

面向游戏开发学习与工程复用的模块化工具包。目标是逐步产出能在明确使用场景中通过商业级验收的实现，适用于不同类型游戏的按需组合。

**当前状态：C++17/CMake 工程骨架可构建，包含库、命令行示例与测试入口；背包已提供实验性规则实现，其他玩法模块尚未实现；尚未经过商业级验证。**

以类 MMO 为参考场景，重点练习系统设计。Godot 后续通过适配层接入，核心库独立于引擎。

- [构建与验证](docs/building.md)
- [架构与学习约定](docs/architecture.md)

## 商业游戏有哪些必需系统？

不存在所有商业游戏都必须拥有的玩法系统。背包、任务、抽卡、邮件、战斗及怪物 AI 都取决于玩法；账号、支付、匹配和运营活动取决于联网与发行模式。

几乎所有游戏都要解决运行生命周期、内容与配置、资源、玩家交互、流程及错误诊断；需要保留进度或设置时必须解决持久化。这些能力可能由引擎或平台提供，不必全部重新实现。测试、构建、版本管理和性能验证属于商业工程的基础能力。

音频、多语言、可访问性和平台服务按产品目标配置。联网游戏还必须明确可信端、同步与故障恢复；持续运营游戏通常还需要运营工具与数据分析。下面的目录是可组合能力清单，不是每款游戏的安装清单，也不是完整游戏引擎。渲染、物理、动画和导航等通常接入引擎；关卡、玩法内容与美术制作仍由具体项目负责。

## 模块目录

### 基础能力

| 模块 | 适用范围 |
| --- | --- |
| [运行时与模块生命周期](modules/foundation/core/README.md) | 通用基础；实现规模按项目调整 |
| [配置与数据定义](modules/foundation/configuration/README.md) | 通用基础 |
| [事件与消息](modules/foundation/events/README.md) | 常见基础；小项目可使用简单调用 |
| [存档与持久化](modules/foundation/persistence/README.md) | 凡是需要保留进度或设置的游戏 |
| [资源管理](modules/foundation/assets/README.md) | 通用基础；可适配引擎自带能力 |
| [场景与流程](modules/foundation/scenes/README.md) | 通用基础 |
| [输入与设备](modules/foundation/input/README.md) | 需要玩家交互的游戏 |
| [界面与交互](modules/foundation/ui/README.md) | 通用基础 |
| [音频](modules/foundation/audio/README.md) | 有声音的游戏 |
| [本地化](modules/foundation/localization/README.md) | 多语言发行；单语言也宜预留边界 |
| [设置与可访问性](modules/foundation/settings/README.md) | 常见基础；按设备和产品需求裁剪 |
| [日志与诊断](modules/foundation/diagnostics/README.md) | 商业级工程基础 |
| [测试与质量工具](modules/foundation/testing/README.md) | 商业级工程基础 |



### 玩法领域模块

| 模块 | 适用范围 |
| --- | --- |
| [背包与物品](modules/gameplay/inventory/README.md) | 拥有可收集、携带或使用物品的游戏 |
| [货币与奖励](modules/gameplay/economy/README.md) | 存在资源交易或奖励的游戏 |
| [装备与属性](modules/gameplay/equipment/README.md) | 存在装备、成长数值的游戏 |
| [成长与解锁](modules/gameplay/progression/README.md) | 存在等级、技能树或长期进度的游戏 |
| [任务与目标](modules/gameplay/quests/README.md) | 存在任务、挑战或目标追踪的游戏 |
| [对话与叙事](modules/gameplay/dialogue/README.md) | 存在对话或交互叙事的游戏 |
| [怪物与 NPC AI](modules/gameplay/ai/README.md) | 存在自主角色的游戏 |
| [战斗与伤害](modules/gameplay/combat/README.md) | 存在战斗的游戏 |
| [技能与效果](modules/gameplay/abilities/README.md) | 存在主动技能或持续效果的游戏 |
| [世界交互](modules/gameplay/interaction/README.md) | 存在场景对象交互的游戏 |
| [合成与生产](modules/gameplay/crafting/README.md) | 存在配方、制造或生产队列的游戏 |
| [掉落与随机奖励](modules/gameplay/loot/README.md) | 存在随机掉落的游戏 |
| [抽卡与保底](modules/gameplay/gacha/README.md) | 存在抽卡玩法的游戏；不属于通用必需能力 |



### 联网与运营模块

| 模块 | 适用范围 |
| --- | --- |
| [网络与同步](modules/services/networking/README.md) | 联网游戏 |
| [账号与身份](modules/services/accounts/README.md) | 需要账号或在线身份的游戏 |
| [邮件与补偿](modules/services/mail/README.md) | 需要系统邮件、奖励附件或运营补偿的游戏 |
| [活动与运营配置](modules/services/liveops/README.md) | 持续运营的游戏 |
| [商店与支付](modules/services/commerce/README.md) | 存在购买、内购或付费内容的游戏 |
| [社交与组队](modules/services/social/README.md) | 存在好友、公会、聊天或队伍的游戏 |
| [匹配与房间](modules/services/matchmaking/README.md) | 存在多人对局的游戏 |
| [排行榜与成就](modules/services/leaderboards/README.md) | 需要排名或成就的游戏 |
| [数据分析](modules/services/analytics/README.md) | 需要产品分析与运营指标的游戏 |
| [平台与发行适配](modules/services/platform/README.md) | 多平台发行或使用平台服务的游戏 |



## 使用与开发原则

按需选择模块，优先使用稳定的引擎或平台能力，通过适配器隔离差异。先在一个真实参考游戏中验证，再扩展复用范围。“适合各种游戏”意味着能力可裁剪和替换，而非所有游戏使用同一套规则或全部模块。

每个模块 README 记录职责、验收重点、计划交付和待讨论问题；目录分组用于导航，不表示已经确定代码包结构或依赖关系。核心实现采用 C++17/CMake；具体 API、存储格式和部署方式随模块设计确定。

- [工程质量要求](docs/quality.md)
- [实施路线与技术栈讨论](docs/roadmap.md)

## Git 约定

仓库本地维护，以小而完整的变更提交。当前没有配置远程仓库。接入引擎后补充引擎专用忽略规则，避免忽略应提交的资源元数据。不要提交密钥、真实用户存档及构建产物。

## 许可证

许可证尚未选定，当前不授予开源分发许可；公开发布前确认代码和素材授权。
