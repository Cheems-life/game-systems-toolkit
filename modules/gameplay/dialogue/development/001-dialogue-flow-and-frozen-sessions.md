# 001：对话图、命令与冻结会话

- 日期：2026-10-24；状态：已验证。
- 代码：dialogue.hpp、dialogue.cpp、archive.cpp；提交主题 `feat: add resumable dialogues and split content loading`。
- 导航：[索引](README.md) · [玩家协调](../../player/development/005-dialogue-and-archive-v4.md) · [拆分配置](../../../foundation/configuration/development/004-dialogue-content-loading.md)。

## 场景、目标与范围

文本、选项、结束三个节点；一个玩家同时最多一个活动会话。流程不依赖引擎、任务、语言正文或完整 Player。暂不实现条件脚本、变量、语音、演出、发奖、接任务或自动 NPC 绑定。

## 数据模型与规则

Definition 包含 ID/版本/入口/完成事实类型和节点。Text、Choice 用稳定 ASCII text_key 引用正文；结束节点没有正文和后继。图节点排序规范化，选项保留配置显示顺序。拒绝缺失引用、不可达节点、没有通往结束路径的节点；允许有出口的人工循环，每命令只走一步。

Catalog 保存不可变共享定义，显式 replace_dialogue_definition 要求改内容时提升版本，相同版本相同内容允许幂等。安装/移除当前定义不影响活动和历史会话；共享对象仅在无人持有时释放。

Command 保存请求 ID、动作、对话 ID 或会话 ID、期望节点/修订号、选项。开始会话 ID=开始请求 ID。继续/选择/完成/取消必须匹配期望节点和修订号，避免过期点击。重放先于当前会话/配置检查；同请求不同原文冲突。完成仅在 end 节点显式确认，取消不发完成事实。

Book 保存冻结会话和严格成功序号的命令回执。默认历史 256，最多 1024，最多 16 会话，每图最多 128 节点、每选项节点最多 8 项，键最多 96 字节，当前目录最多 128 图；不淘汰。回执记录完成事实序号（非完成命令为 0），其真实性由可信协调器保证。

## 方案与选择理由

图与文本分离便于多语言和表格导出。冻结图优于按当前配置解释旧节点；共享图减少候选复制。额外修订号用于循环图中节点 ID 再次出现时仍能拒绝旧点击。显式结束确认可把完成事实与会话状态纳入 Player 同一候选。

## 实现过程与失败处理

命令规范检查/查重 → 在候选执行一次转换 → 准备回执 → swap。任何错误或 bad_alloc 保留原状态。恢复从冻结开始定义重放完整命令日志，比较每条回执与最终会话，不依赖当前目录。内部重建不重复外部副作用。

## 验证

环境：macOS，普通构建 GCC 15.2.0；地址/未定义行为检查构建 AppleClang 17.0.0，C++17。

执行 `cmake -S . -B build`、`cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure --parallel 4`：全工程 50/50 通过。整理新代码格式后再次构建并执行 `ctest --test-dir build -R dialogue --output-on-failure --parallel 4`，对话相关 7/7 通过。

执行 `cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/c++ '-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer'`、构建并运行完整 CTest：50/50 通过，未报告 sanitizer 错误。

`tests/dialogue/core_tests.cpp` 验证图错误、规范化、循环及修订号、显示顺序、过期操作、重放/冲突、取消、活动/历史会话上限、图更新/移除冻结、日志上限和篡改恢复。`archive_tests.cpp` 用 Python struct 独立组装的固定 DDLGARCH v1 / DQSTARCH v4 样本验证精确字节；逐字节截断及内外各 1000 次随机变异（部分重算校验和）检查输出保持。`allocation_tests.cpp` 在一次分配失败后持续拒绝后续分配，逐点扫描命令、目录替换、恢复、编解码及玩家提交，均在成功前保持原状态。

两进程示例保存活动 v1，在新进程空对话/任务目录下恢复、完成、领奖和重放成功。另建 /tmp 外部 CMake 工程，只链接 dialogue / dialogue_configuration，构建与运行成功；链接命令仅有 dialogue、dialogue_configuration、persistence，没有 Player/任务/商城。

未验证：Windows/Linux、线程竞争、生产规模长会话性能、任意输入的穷尽证明、线上分发。

## 限制与下一步

有限日志满后拒绝新命令，尚无归档。图冻结保证流程，不自动冻结 UI 的语言资源；UI 需要固定文本包时持有旧只读资源；跨版本重启还需发布者保留旧键/旧文本包，因为正文不随存档保存。无并发、数据库事务或性能承诺。

## 决策演进

首次实现；公共 API 遵循动作+业务对象命名。
