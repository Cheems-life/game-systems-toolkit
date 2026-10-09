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
