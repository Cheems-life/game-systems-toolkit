# 002：应用拥有交互和对话目录

- 日期：2026-10-25；状态：已实现，普通全流程回归通过。
- 导航：[记录索引](README.md) · [玩家步骤008](../../../modules/gameplay/player/development/008-external-interaction-dialogue-catalogs.md)。

Application已有content_包，其中包含交互目录，新增dialogue_catalog_保存加载并校验好的图目录。构造Player时不再传这两类配置；inspect命令使用content_->interactions()，对话命令使用dialogue_catalog_。玩家Book只保存玩家记录与冻结会话。

相比把目录放Player中，应用负责内容生命周期和发布时机，更容易多玩家共享，避免混淆规则与进度。正文仍由texts_负责。目录必须在调用期间有效且不被同时改写；本应用没有在线热更、线程或发布管理器，不声称已有这些能力。

未改变CLI命令、存档路径、初始资产或存档格式，tests/application的对话中断/重启/领奖/后续购买流程普通测试通过。完整ASan/UBSan结果见玩家008。仅调整装配，不新增重复的领域测试。
