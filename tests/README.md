# 测试入口

各源码文件顶部已标注用途。`*.cpp` 是独立测试程序，`*_test_support.hpp` 是共享辅助代码，根 `CMakeLists.txt` 加载模块子目录，各子目录的 `CMakeLists.txt` 负责构建与注册。

## 目录分类

```text
tests/
├── core/          工程冒烟测试
├── inventory/     背包规则、容器转移与存档格式
├── persistence/   文件读写与安全替换
├── economy/       经济规则、存档、分配失败及经济辅助头文件
├── commerce/      商城规则、存档、分配失败及商城辅助头文件
├── CMakeLists.txt 测试总入口
└── README.md      文件用途索引
```

商城辅助头文件通过 `../economy/economy_test_support.hpp` 显式复用经济测试工具。各目录也有自己的 `CMakeLists.txt`。历史开发记录中的 `tests/<文件名>` 平铺路径，对应下表的同名新文件；历史记录不据此改写原验证结论。

运行方式仍为 `ctest --test-dir build --output-on-failure`。测试名称保持不变；测试可执行文件位于 `build/tests/<模块>/`，示例的保存/加载 fixture 保持原依赖顺序，存档位于各模块构建目录。

| 文件 | 主要用途 |
| --- | --- |
| [framework_smoke.cpp](core/framework_smoke.cpp) | 工程冒烟测试：通过公开头文件调用核心库的版本接口，检查库能编译、链接并运行。 |
| [inventory_tests.cpp](inventory/inventory_tests.cpp) | 背包基础规则测试：检查添加、移除、拆堆、合并、交换及非法参数、容量和整数边界。 |
| [inventory_transactions_tests.cpp](inventory/inventory_transactions_tests.cpp) | 容器转移与快照恢复测试：检查宝箱拾取、仓库双向转移和多容器整体恢复。 |
| [inventory_archive_tests.cpp](inventory/inventory_archive_tests.cpp) | 背包存档格式测试：检查固定版本样本、编码/解码往返、结构限制与多容器恢复。 |
| [persistence_tests.cpp](persistence/persistence_tests.cpp) | 文件持久化测试：在独占临时目录检查有界读取、写入替换、错误返回和临时文件清理。 |
| [economy_tests.cpp](economy/economy_tests.cpp) | 经济规则测试：检查钱包增减、金额上限、混合奖励、购买、请求去重和统一快照恢复。 |
| [economy_archive_tests.cpp](economy/economy_archive_tests.cpp) | 经济存档格式测试：检查余额、容器和成功交易记录的统一编码/解码及恢复后去重。 |
| [economy_allocation_tests.cpp](economy/economy_allocation_tests.cpp) | 经济异常安全测试：仅在本测试程序中重载全局内存分配，逐步注入 std::bad_alloc。 |
| [commerce_tests.cpp](commerce/commerce_tests.cpp) | 商城业务测试：检查商品配置、多份计价、数量溢出、购买失败以及改价/下架/移除后的旧请求重放。 |
| [commerce_archive_tests.cpp](commerce/commerce_archive_tests.cpp) | 商城存档格式测试：检查购买意图与经济回执的关联，以及无当前商品配置时的恢复与重放。 |
| [commerce_allocation_tests.cpp](commerce/commerce_allocation_tests.cpp) | 商城异常安全测试：仅在本测试程序中重载内存分配，逐步注入 std::bad_alloc。 |
| [economy_test_support.hpp](economy/economy_test_support.hpp) | 经济测试公共辅助：提供 CHECK 检查、示例经济对象、交易请求、物品统计和快照比较。 |
| [commerce_test_support.hpp](commerce/commerce_test_support.hpp) | 商城测试公共辅助：复用经济测试工具，创建示例商品目录和商城，比较商城完整快照。 |
| [CMakeLists.txt](CMakeLists.txt) | 加载五个模块测试子目录；子目录分别注册测试及保存/加载 fixture。 |


工程冒烟测试：验证公开头文件、库链接及运行入口能够一起工作。
它不代表玩法规则已通过验证。使用 CTest 执行，不需要下载第三方依赖。

背包已有边界和 10000 次连续操作规则测试。后续按模块添加关键规则测试，必要时再选择测试框架。背包优先覆盖数量守恒、容量边界、操作失败保持原状态；存档覆盖迁移与中断恢复。避免仅复述实现的测试。

新增转移与恢复测试、存档格式测试和文件适配器测试。包含 5000 次连续跨容器操作与 3000 次格式变异；CTest fixture 让保存进程先运行，再启动独立加载进程。文件测试在构建目录创建独占临时目录，正常结束清理；测试失败可能留下诊断文件。

经济测试包含钱包与交易规则、规范化去重、恢复、编码和 bad_alloc 逐分配点注入。独立示例进程验证存档后的请求去重，注入分配器只存在于对应测试可执行文件中，不影响生产库。

商城新增规则、格式、分配失败和跨进程测试，覆盖原经济 clone 的独立性与容量限制。商城和经济开发记录分别说明跨模块提交边界，不将本地内存事务当作服务端数据库事务。
