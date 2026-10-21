# 001：每玩家交互点与成功记录

- 日期：2026-10-22；状态：已验证（实验性）。
- 关联代码：interaction.hpp、interaction.cpp；提交主题 `feat: add inspect interactions with atomic quest delivery`。
- 导航：[开发索引](README.md) · [玩家协调](../../player/development/003-interaction-and-archive-v3.md) · [配置接入](../../../foundation/configuration/development/003-interaction-schema.md)。

## 场景、目标与范围

检查遗迹、阅读告示牌等无物品效果交互；每玩家一次或重复。独立领域不依赖任务/玩家/引擎。距离、视线、对象实例存活、身份、多人共享竞争由可信调用方裁决，不在本版保证范围。

## 数据模型与规则

Catalog 保存点 ID、版本、enabled、Mode(once/repeatable)、fact_type。Request 只有 request_id 与 point；Receipt 冻结请求、配置版本、模式、事实类型及事件序号。成功效果固定为 {type=fact_type,subject=point,quantity=1}，不引入任意脚本。

Book 属于一个玩家，持有请求记录和由记录导出的每点模式索引。once 的成功历史永久阻止换请求再次交互；repeatable 允许新请求。每点模式在首次成功后固定，当前配置改变模式报配置冲突，不通过改版本重置。请求原文相同返回原记录，冲突报错；重放先于当前目录、enabled、容量和新序号检查。交互请求 ID 独立于经济请求 ID，事件序号属于共享可信事实流。

配置最多 4096 点；历史默认 256，最多 4096，不淘汰。事件 ID 正数且成功记录严格递增，可有外部事实造成的间隔。

## 方案与选择理由

单独保存 completed 布尔会与请求记录漂移；由成功记录重建每点模式。独立 Book.execute 仅负责自身一致性，Player 通过候选组成任务投递边界；不向底层传完整玩家。一次/重复模式是生命周期规则，冻结避免改表意外重新开放一次性内容。版本与事实类型可按当前目录改变重复交互，旧请求仍返回冻结结果。

## 实现过程与失败处理

validate request → 查重放 → query 同一套条件 → 校验序号 → 复制候选记录/索引 → 准备返回结果 → 无异常 swap。所有正常错误与 bad_alloc 不改变 Book。restore 从有界、按事件排序记录构建候选，验证唯一请求、模式一致和 once 仅一次，再提交。无文件或外部副作用。

## 验证

实际验证（2026-10-22，macOS arm64）：

- Homebrew GCC 15.2.0 Debug：`cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure`，完整 43/43 通过。
- AppleClang 17 Debug，build/sanitized 启用 `-fsanitize=address,undefined -fno-omit-frame-pointer`；同样构建后完整 CTest 43/43 通过，无 Sanitizer 报告。修正初次构建发现的缩进警告后，最终构建无编译警告。
- 相比此前 35 项，新增 8 项：独立交互核心、玩家、格式、分配注入、配置、配置命令检查与双进程 save/load。新增文件头部均解释用途。
- 核心覆盖非法定义/上限、开启/关闭、一次与重复、请求冲突、改表/删表重放、版本冻结、模式漂移、历史满、失败不改与恢复重建。
- 协调覆盖接取边界 100→101、外部事实 200→201、uint64 序号耗尽、任务日志满不消耗、65 匹配失败后领取一个任务再以原请求重试成功、购买/领奖保留交互记录。另补一次点先于任务完成、不补算的用例，普通/ASan/UBSan 定向回归通过。
- 旧 v1/v2 固定样本继续读取并升级 v3；独立 Python struct 固定 v3 样本通过字节校验、恢复和重放。格式测试检查凭证/原始事实关联、重复请求/事件、非法模式、所有截断及 1500 次确定性字节变异。
- 持续 bad_alloc 逐分配点覆盖 Book.execute/restore、Player.interact/restore、v3 编解码和 v3 配置加载；失败保留原对象或输出。示例用 1 条日志预算触发失败，在更大预算 Player 恢复同一状态后用原请求重试成功，再保存并由独立进程以空目录恢复。
- 独立 CMake 消费方仅链接 designer::interaction，构建/运行成功，最终链接命令只有 libdesigner_interaction.a，没有任务/玩家/JSON；子项目默认关闭示例与测试。
- 本地 Markdown 链接及本次提交内容的行尾空白检查通过。现有配置与玩家文件的用户格式/注释保留在工作区，不纳入功能提交。
- 未验证：其他平台、引擎距离/权限裁决、全服共享世界状态、并发/数据库事务、真实断电、生产规模性能和系统实际内存耗尽。故障注入不等于任意资源故障可恢复。

## 限制与下一步

没有跨玩家世界状态、宝箱奖励、场景回调、取消、冷却或长期日志归档。历史满后拒绝新交互。查询是瞬时提示，实际请求仍复查。

一次点允许无任务时成功；之后接取任务不补算且无法再次使用。没有将“必须有活动任务”耦合进交互条件；需要历史状态目标时另行设计。

## 决策演进

首次实现。跨系统提交与保存格式归玩家记录，不把独立 Book 的提交当作任务提交。
