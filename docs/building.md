# 构建与验证

需要 CMake 3.20 或更高版本，以及支持 C++17 的编译器。当前不需要 Godot、数据库或下载第三方库。

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
