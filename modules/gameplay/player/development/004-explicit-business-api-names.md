# 004：明确业务接口命名

- 日期：2026-10-24；状态：已实现，验证结果见下文。
- 场景：用户无法从 query 等泛化名称辨认业务动作，要求调整并检查、提交现有学习注释。
- 导航：[本系统记录](README.md) · [交互命名](../../interaction/development/002-explicit-interaction-api.md) · [任务命名](../../quests/development/009-explicit-quest-api.md)。

## 规则与方案

公共动作采用动词与业务对象，保留现有 C++ snake_case。只改接口名字，不改参数、返回值、所有权、去重、候选提交或文件格式。查询名描述返回的任务状态或交互资格，修改名说明操作对象。

| 旧 Player 接口 | 新接口 | 用途 |
| --- | --- | --- |
| query | query_quest_status | 查询任务状态 |
| accept | accept_quest | 接取任务 |
| apply | apply_quest_progress_event | 提交明确目标的进度事件 |
| observe | observe_gameplay_fact | 观察可信事实，匹配目标绑定 |
| purchase | purchase_product | 购买商品 |
| claim | claim_quest_reward | 领取任务奖励 |
| query_interaction | check_interaction_availability | 检查新交互资格 |
| interact | execute_interaction | 记录交互并统一投递任务事实 |

仅加注释仍无法解决调用点可读性；保留模糊旧名作转发会让两套名称长期并存，因此本版不保留旧入口。snapshot/restore 和常规目录/容器方法保持原名；本次不扩大到所有系统做无差别更名。

## 实施与失败边界

统一更新任务核心、交互簿与玩家协调器，迁移所有示例、配置集成及测试；文档中的当前接口同步修改，历史步骤不覆写。业务行为不变，语法层为破坏性源代码变更，调用方按表更新即可；存档不需要迁移。

## 用户修改检查

保留配置加载器、JSON 读取器、交互和玩家模块的注释与格式。using 声明顺序调整无行为影响，错误宏换行保留续行，语义与原单行宏一致。修正“交互点点”笔误、将 fact_type 注释明确为事实类型、补全购买空注释，清理行尾空白。没有发现需撤销的业务逻辑变化。

## 验证

- 环境：macOS，Debug 使用 GCC 15；cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug、cmake --build build --parallel、ctest --test-dir build --output-on-failure，完整 43/43 通过。
- 内存检查：AppleClang 17，build/sanitized 保留 -fsanitize=address,undefined -fno-omit-frame-pointer，重新构建及完整测试 43/43 通过。
- 外部 CMake 工程链接 designer::player，直接编译调用新的任务、交互和玩家 API 并运行成功；覆盖交互资格、执行、凭证查询、任务查询/接取/事件/事实和购买/领奖入口。
- 当前源码旧接口定义检查、本地 Markdown 链接与 git diff --check 均通过。历史记录保留旧名作追溯。
- 未新增业务能力，未额外做数据库、并发、平台或负载验证。

## 限制与演进

不增加运行时功能，没有新增数据库、并发或持久化保证；沿用既有回归覆盖。替代此前步骤中使用的短接口名，事务与状态规则保持有效。后续开发遵循根 AGENTS.md 的业务命名约定。
