# 003：商城、经济状态与购买记录统一存档

- 状态：已验证（本机规则及异常安全范围）。
- 日期：2026-10-17。
- 关联代码或提交：本次商城第一版，提交主题 `feat: add versioned shop purchases and persistent replay`。
- 导航：[本系统索引](README.md) · [经济步骤 005](../../../gameplay/economy/development/005-owned-candidate-clone.md)。

## 场景、目标与范围

成功购买后保存，重启时当前商品已经移除，仍能恢复资产并重放原购买。损坏关联或无效资产不能只恢复一部分。

## 数据模型与规则

Snapshot 嵌入完整经济快照，并按经济 sequence 递增保存 PurchaseReceipt。序号可以有间隔，因为先前存在非商城交易。DSHPARCH v1 存经济字节、购买意图、版本及经济序号；交易正文只在经济字节里编码一次。请求 ID 和序号匹配后，解码器恢复完整回执。固定小端、16 MiB、最多 4096 条购买记录，FNV-1a 仅检查意外损坏。

## 方案与选择理由

分文件保存资产/购买意图会产生版本错位。每份商城记录重复编码完整交易会扩大文件并产生双份正文校验负担；因此编码时用关联，内存对象保留完整回执方便调用。恢复不访问当前商品目录，否则删除商品或改价会使旧存档失效。

不能仅凭关联证明价格历史真实；没有签名、旧配置存储或余额轨迹重放，本步只保证内部结构一致。

## 实现过程与失败处理

1. 编码前校验购买与经济记录的一致性，生成嵌套经济字节。
2. 固定宽度写入意图和关联，末尾写校验。成功才替换输出。
3. 解码先限制文件、版本和校验，解析经济内容再按 ID/序号寻找关联，拒绝重复、零值、尾部垃圾等。
4. Shop::restore 再用当前资产规则恢复经济候选，并构造商城历史。
5. 全部完成后统一指针交换。解码只在成功时移动结果，编译期检查无异常赋值。
6. 文件保存仍组合 persistence，内存成功与文件落盘是两个边界。

## 验证

测试包括截断每一个前缀、校验损坏、未来版本、尾部垃圾、超限计数、有效外层校验但经济序号错误、重复/非法关联、独立构造固定小端格式样本、2000 次变异、编码/解码/恢复分配失败以及 save/load 两个进程。示例加载使用空商品目录。

- 已执行：macOS，现有 Debug 构建（GCC 15.2.0）；构建后完整 CTest 17/17 通过，后续新增边界的商城 5 项复查通过。
- 命令：`cmake --build build --parallel`、`ctest --test-dir build --output-on-failure`；示例 save/load 使用 `build/example-commerce.sav`。
- 内存检查：AppleClang 17 的 Debug 构建启用 AddressSanitizer/UndefinedBehaviorSanitizer，完整 CTest 17/17 通过。命令：`cmake -S . -B build/sanitized -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"`，随后构建并执行 CTest。
- 外部集成：独立 CMake 工程通过 add_subdirectory 仅链接 designer::commerce 与 designer::persistence，默认不构建本工程示例和测试；AppleClang 构建运行通过。
- 文档检查：本地 Markdown 链接与 git diff --check 通过。
- 未覆盖：真实断电、并发、服务端数据库、平台支付及性能预算。

## 限制与下一步

不会把经济历史中缺失的商城意图补造出来，缺失意图的同 ID 后续会冲突。没有自动格式迁移、防回滚或防伪造。当前文件适配器未覆盖真实断电及所有平台。

## 决策演进

首次商城格式；嵌入既有 DECOARCH v1，不改变经济格式或容器格式。旧纯经济文件可先按旧入口恢复再交给商城，但不会自动产生购买历史。
