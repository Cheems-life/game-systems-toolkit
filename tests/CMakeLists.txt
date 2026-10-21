# 测试总入口：按功能模块加载测试；具体目标、依赖和示例 fixture 由各子目录维护。
# CTest 名称保持不变，仍可从顶层构建目录运行全部测试或按名称筛选。

add_subdirectory(core)
add_subdirectory(inventory)
add_subdirectory(persistence)
add_subdirectory(economy)
add_subdirectory(commerce)

add_subdirectory(quests)

add_subdirectory(configuration)

add_subdirectory(interaction)
