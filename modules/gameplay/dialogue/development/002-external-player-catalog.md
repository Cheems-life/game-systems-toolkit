# 002：当前对话目录由应用持有

日期：2026-10-25。底层Book不变，Player::execute_dialogue_command新增const Catalog&参数；移除Player目录和替换入口。应用更新自己的Catalog再在后续命令中传入。

活动会话持有冻结Definition，目录被销毁、替换或移除不会修改旧会话；历史重放先于当前目录查询。新start按传入目录开始。更改所有权不改节点、revision、完成事实或存档格式。

详见[玩家008](../../player/development/008-external-interaction-dialogue-catalogs.md)。原有空目录恢复、继续/重放、旧会话与新版新会话、故障分配测试已更新并在普通全回归通过；无并发或数据库保证变化。
