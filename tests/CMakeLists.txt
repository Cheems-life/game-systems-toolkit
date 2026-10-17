# 测试构建入口：创建并注册各模块的独立测试程序，链接所需领域库。
# 分配失败注入各自隔离在独立可执行文件；示例启用且在 UNIX 时，用 fixture 安排保存进程先于加载进程。

add_executable(designer_framework_smoke framework_smoke.cpp)
target_link_libraries(designer_framework_smoke PRIVATE designer::core)
set_target_properties(designer_framework_smoke PROPERTIES CXX_EXTENSIONS OFF)
designer_enable_warnings(designer_framework_smoke)
add_test(NAME designer.framework.smoke COMMAND designer_framework_smoke)

add_executable(designer_inventory_tests inventory_tests.cpp)
target_link_libraries(designer_inventory_tests PRIVATE designer::inventory)
set_target_properties(designer_inventory_tests PROPERTIES CXX_EXTENSIONS OFF)
designer_enable_warnings(designer_inventory_tests)
add_test(NAME designer.inventory.rules COMMAND designer_inventory_tests)

foreach(test IN ITEMS inventory_transactions inventory_archive persistence)
    add_executable(designer_${test}_tests ${test}_tests.cpp)
    target_link_libraries(designer_${test}_tests PRIVATE designer::inventory)
    set_target_properties(designer_${test}_tests PROPERTIES CXX_EXTENSIONS OFF)
    designer_enable_warnings(designer_${test}_tests)
    if(test STREQUAL "persistence")
        target_link_libraries(designer_${test}_tests PRIVATE designer::persistence)
        add_test(NAME designer.${test} COMMAND designer_${test}_tests ${CMAKE_CURRENT_BINARY_DIR})
    else()
        add_test(NAME designer.${test} COMMAND designer_${test}_tests)
    endif()
endforeach()

if(DESIGNER_BUILD_EXAMPLES AND UNIX)
    add_test(NAME designer.storage_demo.save COMMAND designer_inventory_storage_demo
        ${CMAKE_CURRENT_BINARY_DIR}/demo-player.sav)
    set_tests_properties(designer.storage_demo.save PROPERTIES FIXTURES_SETUP saved_inventory)
    add_test(NAME designer.storage_demo.load COMMAND designer_inventory_storage_demo
        ${CMAKE_CURRENT_BINARY_DIR}/demo-player.sav --load)
    set_tests_properties(designer.storage_demo.load PROPERTIES FIXTURES_REQUIRED saved_inventory)
endif()

foreach(test IN ITEMS economy economy_archive economy_allocation)
    add_executable(designer_${test}_tests ${test}_tests.cpp)
    target_link_libraries(designer_${test}_tests PRIVATE designer::economy)
    set_target_properties(designer_${test}_tests PROPERTIES CXX_EXTENSIONS OFF)
    designer_enable_warnings(designer_${test}_tests)
    add_test(NAME designer.${test} COMMAND designer_${test}_tests)
endforeach()

if(DESIGNER_BUILD_EXAMPLES AND UNIX)
    add_test(NAME designer.economy_demo.save COMMAND designer_economy_example
        ${CMAKE_CURRENT_BINARY_DIR}/economy-player.sav)
    set_tests_properties(designer.economy_demo.save PROPERTIES FIXTURES_SETUP saved_economy)
    add_test(NAME designer.economy_demo.load COMMAND designer_economy_example
        ${CMAKE_CURRENT_BINARY_DIR}/economy-player.sav --load)
    set_tests_properties(designer.economy_demo.load PROPERTIES FIXTURES_REQUIRED saved_economy)
endif()

foreach(test IN ITEMS commerce commerce_archive commerce_allocation)
    add_executable(designer_${test}_tests ${test}_tests.cpp)
    target_link_libraries(designer_${test}_tests PRIVATE designer::commerce)
    set_target_properties(designer_${test}_tests PROPERTIES CXX_EXTENSIONS OFF)
    designer_enable_warnings(designer_${test}_tests)
    add_test(NAME designer.${test} COMMAND designer_${test}_tests)
endforeach()

if(DESIGNER_BUILD_EXAMPLES AND UNIX)
    add_test(NAME designer.commerce_demo.save COMMAND designer_commerce_example
        ${CMAKE_CURRENT_BINARY_DIR}/commerce-player.sav)
    set_tests_properties(designer.commerce_demo.save PROPERTIES FIXTURES_SETUP saved_commerce)
    add_test(NAME designer.commerce_demo.load COMMAND designer_commerce_example
        ${CMAKE_CURRENT_BINARY_DIR}/commerce-player.sav --load)
    set_tests_properties(designer.commerce_demo.load PROPERTIES FIXTURES_REQUIRED saved_commerce)
endif()
