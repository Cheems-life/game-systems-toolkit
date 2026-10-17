# 001：商品与购买意图模型

- 状态：已验证（本机规则及异常安全范围）。
- 日期：2026-10-17。
- 关联代码或提交：本次商城第一版，提交主题 `feat: add versioned shop purchases and persistent replay`。
- 导航：[本系统索引](README.md) · [经济步骤 005](../../../gameplay/economy/development/005-owned-candidate-clone.md)。

## 场景、目标与范围

一份商品包含 5 瓶药水，玩家买 3 份；服务端应根据可信单价计算三倍成本及 15 瓶奖励。请求不携带价格和奖励。本步只实现游戏币商城，不做真实支付、库存、折扣、限购和 UI。

## 数据模型与规则

`ProductDefinition` 保存非零 ID、非零配置版本、上下架、单份 Cost/RewardBundle；`PurchaseRequest` 只保存请求 ID、商品 ID、份数和目标容器。商品目录最多 4096 项，各成本/奖励列表最多 64 项，正数、同列表无重复。目录构造时排序，结构非法抛出 invalid_argument。货币/物品跨表引用在经济执行中验证。只读 entries 支持按 ID 遍历。

## 方案与选择理由

比较了客户端自报价、为每份调用一次经济操作及一次构造总交易。客户端自报价不可信；逐份交易可能部分成功。因此先在本地检查每一项乘法，再构造单笔总交易。检查公式为 unit <= max / quantity，quantity 已保证非零。重复配置条目直接拒绝，避免策划配置中的重复项被无意解释成叠加价格；运行时经济层仍保留自己的规范化规则。

## 实现过程与失败处理

1. 构造目录，排序并校验配置，保留自有副本。
2. 请求验证四个非零字段；已成功请求先走历史分支。
3. 新请求查询商品与上架状态，计算总成本/奖励。
4. 数量溢出返回 quantity_overflow；未知或下架商品分别返回 unknown_product / product_unavailable。
5. 剩余余额和背包规则交给经济模块，失败不修改资产。

replace_catalog 交换完整已验证目录，指针视图随之失效。配置版本由可信发布方维护，不自动检查递增或报价有效期。

## 验证

规则测试覆盖非法配置、排序、双份购买、纯货币奖励、三个列表乘法溢出、最大单价不溢出但余额不足、模型对照 200 次购买。

- 已执行：macOS，现有 Debug 构建（GCC 15.2.0）；构建后完整 CTest 17/17 通过，后续新增边界的商城 5 项复查通过。
- 命令：`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例 save/load 使用 `build/example-commerce.sav`。
- 内存检查：AppleClang 17 的 Debug 构建启用 AddressSanitizer/UndefinedBehaviorSanitizer，完整 CTest 17/17 通过。命令：`cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`，随后构建并执行 CTest。
- 外部集成：独立 CMake 工程通过 add_subdirectory 仅链接 designer::commerce 与 designer::persistence，默认不构建本工程示例和测试；AppleClang 构建运行通过。
- 文档检查：本地 Markdown 链接与 git diff --check 通过。
- 未覆盖：真实断电、并发、服务端数据库、平台支付及性能预算。

## 限制与下一步

没有实现客户端报价确认或价格锁定；失败后新尝试按当前目录计算。名称、图标、翻译由表现层负责。增加库存与限购时必须把对应状态纳入统一提交。

## 决策演进

首次设计。当前结构拒绝免费商品；免费领取仍由经济模块提供，不修改旧经济奖励语义。
