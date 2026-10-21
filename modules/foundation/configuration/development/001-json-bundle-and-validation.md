# 001：JSON 配置包与跨表校验

- 状态：已验证（实验性实现）。
- 日期：2026-10-21。
- 关联代码：`include/designer/configuration/bundle.hpp`、`src/bundle.cpp`、`src/json_reader.hpp`；提交主题 `feat: add validated JSON game configuration bundles`。
- 导航：[开发索引](README.md)、[模块说明](../README.md)、[任务配置冻结](../../../gameplay/quests/development/003-event-boundaries-and-frozen-progress.md)。

## 场景、目标与范围

把物品、货币、商城、任务的 C++ 配置数据迁移到一份 JSON 文件。启动时先校验完整包，再构建业务对象。提供校验命令与集成示例；保留旧示例，作为不依赖配置适配器的 API 教材。不实现热更新、Excel 导入、事件绑定、存档迁移或玩家初始资产配置。

## 数据模型与规则

`schema_version=1` 固定文件结构，正整数 `content_version` 标识调用方的内容批次；不自动比较版本、防回滚或验证同号内容一致性。四张表均必须出现，允许空表。商品、任务、目标规则仍保留独立版本。包只读，持有四个 Catalog 及校验所用 ObjectiveRegistry；调用方复制目录构建业务对象。

配置适配器目标 designer::configuration 依赖 inventory/economy/commerce/quest_core 及私有 persistence 文件读取；领域模块不反向依赖配置或 JSON。目录分类不等于依赖层级，本步不引入通用配置反射框架。JSON 第三方类型不进入公共头。

只接受精确正整数，拒绝浮点数、字符串数值、未知字段、重复对象键和重复业务 ID。奖励和价格引用必须存在；目标类型和规则版本须受已注册处理器支持。关闭商品也校验引用。启动配置不检查某个玩家是否有足够钱/背包空间，这属于业务执行。

输入最多 4 MiB，嵌套最多 32 层，物品最多 4096 项；其他表、子表沿用领域上限。错误提供 source、字段路径、可用时的记录 ID、原因；语法错误还提供字节位置。首错停止，不收集所有错误。语法错误、重复键、深度或数字解析错误可能只能定位根路径；不是所有失败都有记录 ID。

## 方案与选择理由

选择单文件完整包以明确四表属于同一输入批次，避免多文件读到不同发布时刻。未来 Excel 可导出此格式。选择固定版本 nlohmann/json 3.12.0 而非自行编写 JSON 语法解析器；将头和 MIT 许可证纳入 third_party，正常构建无需联网。结构层严格检查未知字段，减少策划拼写错误；代价是新增字段需要升级读取端/格式。

实现初期使用第三方 DOM；持续 bad_alloc 注入触发进程终止。检查 vendored `json_value::destroy` 发现其容器清理会分配辅助堆栈，因此最终改用同库 SAX 接口和仅供本模块使用的 variant 树，根容器算第 1 层、最多 32 层。树和解析器帧用标准 RAII 管理；语法、UTF-8 和数字词法仍由第三方完成，没有自写 JSON 语法解析器。最终异常测试验证了清理及错误诊断路径。

不新增共享全局配置管理器；返回独立完整包就能满足启动场景。也不顺便为领域目录加入运行时替换接口。

## 实现过程与失败处理

读取有界字节 → 有深度/重复键检查的 JSON 解析 → 逐行类型和范围检查 → 领域目录规则与目标处理器校验 → 跨表引用 → 构造完整只读包。每一步使用局部所有权，成功返回包；数据失败返回一个诊断和空包，不发布任何局部表，不修改玩家或旧包。bad_alloc 等资源异常向上传递；不会伪装为配置错误。没有数据库事务、多线程原子操作或外部副作用。

## 验证

- 环境：macOS arm64；普通 Debug 使用 Homebrew GCC 15.2.0；Sanitizer Debug 使用 AppleClang 17.0.0。
- 普通构建：`cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`、`cmake --build build --parallel 4`、`ctest --test-dir build --output-on-failure`。
- Sanitizer：`cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=/usr/bin/c++ -DCMAKE_CXX_FLAGS='-fsanitize=address,undefined -fno-omit-frame-pointer'`；构建后运行 `ctest --test-dir build/sanitized --output-on-failure`。
- 下游：在忽略目录 build/config-consumer-source 创建独立 CMake project，add_subdirectory 本工程，只链接 designer::configuration，编译运行读取空表包；示例/测试选项默认 OFF。
- 场景：类型/边界、重复、版本、引用、处理器扩展、文件错误、坏输入无包、旧包不变、配置驱动购买和领奖、离线构建。
- 实际结果：普通完整 CTest 29/29 通过；ASan/UBSan 完整 CTest 29/29 通过。新增 6 个 CTest（格式/引用、业务集成、异常分配、校验命令及两组内容示例）。无编译警告或 Sanitizer 报告。
- 边界实测：4 MiB 整包允许、超限拒绝；32 层容器进入结构校验、33 层拒绝；四张表及目标/资产子表超限拒绝；uint64 最大值精确保留，超限数字、浮点和字符串拒绝；重复 JSON 键（含转义同名键）与重复业务 ID 拒绝。
- 自定义处理器从注册、配置加载到 QuestBook 推进完成均通过；错误包不影响已加载包、玩家资产、容器、购买记录或任务历史。bad_alloc 持续失败逐点注入覆盖成功包、非法引用、语法错误及重复键路径。确定性字节扰动每隔 7 字节注入非法字节，检查不返回半套包。
- 独立消费方构建并运行成功，不包含 JSON 头，不链接玩家模块。正常构建仅使用仓库内依赖、没有下载步骤；未另外模拟整机断网。
- 手动校验示例成功；把物品引用改为 9999 后，命令返回 1 并定位 `$.products[0].unit_reward.items[0].item`、record 100。第二组商品 200 / 任务 2 的购买、领奖及重放成功。
- 上游头文件 SHA-256 与 v3.12.0 发布页记录一致：`aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63`；许可证已保留；新增/修改文档本地链接校验通过。
- 尚未覆盖：Windows/Linux 编译运行、生产规模性能/内存测量、系统真正耗尽内存或进程被系统终止、任意第三方处理器正确性。故障注入只验证本程序指定的分配点，不代表所有资源故障可恢复。

## 限制与下一步

配置版本尚不写入玩家存档；既有任务冻结规则和购买凭证保持不变。没有任务事件绑定、在线热更、签名、编辑器或全部游戏配置统一方案。内存占用随解析节点及配置条目增长，4 MiB 字节限制不等于 4 MiB 内存限制。不对生产规模、恶意配置处理吞吐做保证。

## 决策演进

首次实现配置模块，不替代领域 Catalog 和历史冻结规则。
