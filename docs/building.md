# 构建与验证

需要 CMake 3.20 或更高版本，以及支持 C++17 的编译器。当前不需要 Godot、数据库或构建时联网下载；JSON 解析头文件及许可证已固定版本纳入 third_party。

从仓库根目录执行：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/examples/designer_quickstart
```

使用 Visual Studio 等多配置生成器时，构建加 `--config Debug`，CTest 加 `-C Debug`；示例通常位于 `build/examples/Debug/designer_quickstart.exe`。Windows 普通单配置生成器的可执行文件也带 `.exe`。

只构建核心库：

```sh
cmake -S . -B build/minimal -DDESIGNER_BUILD_EXAMPLES=OFF -DDESIGNER_BUILD_TESTS=OFF
cmake --build build/minimal --parallel
```

## 嵌入其他 CMake 工程

```cmake
add_subdirectory(path/to/Designer designer-build)
target_link_libraries(your_target PRIVATE designer::core)
```

以子项目方式使用时，示例与测试默认关闭；C++17 要求及公开头文件路径通过目标传递。警告只作用于本工程自己的目标，不强迫下游采用相同警告设置。

目前只提供源码构建和 `add_subdirectory` 集成，不提供 `find_package` 安装包。已验证的平台会记录在变更交付中，未验证平台不宣称可用。

背包示例：`./build/examples/designer_inventory_example`。独立链接目标为 `designer::inventory`，不需要链接 core。

## VS Code IntelliSense

用 VS Code 打开整个仓库根目录。仓库的 `.vscode/settings.json` 设置 C++17 默认解析标准，并指定 `build/compile_commands.json`。生成该文件：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

编译数据库让 Microsoft C/C++ 扩展使用真实的编译器参数与头文件路径；未匹配到编译单元时使用 C++17 默认值。此方式适用于 Makefiles/Ninja 生成器。若使用其他构建目录，需同步修改设置中的路径。安装 CMake Tools 时，仓库设置也会启用编译数据库导出。

若 `std::optional` 仍有红线，通过命令面板执行 `C/C++: Reset IntelliSense Database` 后重新打开文件。必要时执行 `C/C++: Log Diagnostics` 查看实际标准、编译器和配置来源；个人配置或其他配置提供者可能覆盖默认设置。编辑器诊断与真实编译是两个独立过程。

宝箱/仓库/存档示例（替换指定示例文件，然后在下一进程只加载）：

```sh
./build/examples/designer_inventory_storage_demo build/example-player.sav
./build/examples/designer_inventory_storage_demo build/example-player.sav --load
```

链接存档组合示例时使用 `designer::inventory` 与 `designer::persistence`。当前原子保存仅提供 POSIX 适配，并在 macOS 验证；Windows 保存返回 `unsupported_platform`，对应跨进程保存示例测试仅在 POSIX 注册。

经济示例：

```sh
./build/examples/designer_economy_example build/example-economy.sav
./build/examples/designer_economy_example build/example-economy.sav --load
```

链接经济领域库用 `designer::economy`（传递链接 inventory）；需要本地文件时再加 `designer::persistence`。新经济格式与原容器格式使用不同魔数，不自动迁移。示例验证购买失败不扣款及重启后的请求去重。

商城示例（首条替换指定示例存档，第二条用空商品目录恢复并重放）：

```sh
./build/examples/designer_commerce_example build/example-commerce.sav
./build/examples/designer_commerce_example build/example-commerce.sav --load
```

链接领域库使用 `designer::commerce`（传递依赖 economy/inventory）；文件读写再加 `designer::persistence`。商城保存使用独立 DSHPARCH 格式，不自动覆盖或迁移旧经济文件。

任务与共享玩家示例：

```sh
./build/examples/designer_quests_example build/example-quests.sav
./build/examples/designer_quests_example build/example-quests.sav --load
```

只需任务核心时链接 `designer::quest_core`；需要商城/任务共享资产、领奖及完整存档时链接 `designer::player`。文件存储另加 `designer::persistence`。测试按模块位于 `tests/quests/`，CTest 可用 `-R quests` 筛选；存取示例仅在已启用示例的 UNIX 平台注册。

## JSON 配置加载

```sh
./build/examples/designer_config_check examples/configuration/game.json
./build/examples/designer_config_example examples/configuration/game.json
./build/examples/designer_config_example examples/configuration/game.json 200 2
ctest --test-dir build -R configuration --output-on-failure
```

链接 `designer::configuration`；需要共享玩家协调再链接 `designer::player`。固定 JSON 依赖只在适配器内部使用，不暴露第三方头文件路径。校验命令只读文件、不创建玩家，失败返回非零退出码，可手动接入构建/CI。JSON 样例同时覆盖新增商品与任务，规则见 [配置模块](../modules/foundation/configuration/README.md)。
