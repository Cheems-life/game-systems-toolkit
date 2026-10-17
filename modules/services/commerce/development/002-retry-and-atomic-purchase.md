# 002：历史重放与统一购买提交

- 状态：已验证（本机规则及异常安全范围）。
- 日期：2026-10-17。
- 关联代码或提交：本次商城第一版，提交主题 `feat: add versioned shop purchases and persistent replay`。
- 导航：[本系统索引](README.md) · [经济步骤 005](../../../gameplay/economy/development/005-owned-candidate-clone.md)。

## 场景、目标与范围

请求 42 已按每份 100 金币成功买两份；随后单价改到 150、商品下架或移除，同请求必须返回原来的 200 金币回执，不能重新计价或再次发奖。

## 数据模型与规则

Shop 以 unique_ptr<State> 拥有独立 Economy 和购买回执 map，另持有当前商品目录。购买回执保存原意图、商品版本及完整经济 Receipt。同 ID 同意图返回原回执；同 ID 改份数/商品/目标报冲突；失败不记录。商城记录默认 256，上限 4096；经济历史另有上限。

## 方案与选择理由

只保留经济交易无法区分两个价格相同但商品 ID 不同的意图；重试查最新配置又会把价格变化误报成请求冲突。因此商城单独保留意图，先查历史再查配置。

比较了先改资产后写商城记录、持有可写外部 Economy 和拥有候选对象。前两者分别存在异常窗口和外部绕过入口。因此选择接管经济所有权、clone 候选，在两层记录和返回对象都准备完毕后交换 Shop 状态指针。没有引入通用事务基类或模块回调框架。

## 实现过程与失败处理

1. 校验意图；查商城历史，完成重放/冲突判断。
2. 新请求检查记录容量、商品和乘法。
3. 复制经济与商城历史为候选。经济 execute 只修改候选。
4. 如果经济层返回已有请求重放或冲突，但商城无该意图，拒绝收编为商城订单。
5. 准备商城记录和返回值；static_assert 检查返回值无异常移动。
6. 用 noexcept 指针交换提交。任何前置业务失败或 bad_alloc 均保留旧状态。

记录满后仍可重放旧请求。商城和经济共享玩家请求 ID 空间；已存在的非商城记录可保留，但 ID 不可复用。

## 验证

测试覆盖改价新/旧请求、下架和移除后重放、同 ID 不同意图、失败后同意图重试、余额/容量失败不扣款、双层记录容量、完全相同的既有经济回执也不被收编。分配测试在已有订单的状态下逐个失败点执行下一笔购买，验证复制历史、经济回执和商城回执阶段都不会半提交。

- 已执行：macOS，现有 Debug 构建（GCC 15.2.0）；构建后完整 CTest 17/17 通过，后续新增边界的商城 5 项复查通过。
- 命令：`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例 save/load 使用 `build/example-commerce.sav`。
- 内存检查：AppleClang 17 的 Debug 构建启用 AddressSanitizer/UndefinedBehaviorSanitizer，完整 CTest 17/17 通过。命令：`cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`，随后构建并执行 CTest。
- 外部集成：独立 CMake 工程通过 add_subdirectory 仅链接 designer::commerce 与 designer::persistence，默认不构建本工程示例和测试；AppleClang 构建运行通过。
- 文档检查：本地 Markdown 链接与 git diff --check 通过。
- 未覆盖：真实断电、并发、服务端数据库、平台支付及性能预算。

## 限制与下一步

资产视图在成功购买/恢复后失效，目录视图在 replace_catalog 后失效。接管所有权后不得使用旧可写别名。候选及内部经济执行会多次复制数据；未压测。仅单线程，不等于数据库提交或长期请求表。

## 决策演进

新增商城协调边界，补充而非替代经济步骤 002、003 的独立交易规则。经济候选能力见步骤 005。

## 2026-10-17 维护补充

提交主题 `docs: preserve economy and shop learning annotations`：保存经济恢复流程及商城模型/流程的中文学习注释和排版整理。对三处源码忽略注释与空白后比较词法记号，结果与原提交一致；没有改变接口、数据格式或业务规则。执行 `cmake --build build --parallel` 与完整 CTest，17/17 通过。本次未重复运行内存检查。
