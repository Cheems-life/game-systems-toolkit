# 背包测试：基础规则、跨容器转移/恢复和存档格式，以及独立进程保存/加载示例。
add_executable(designer_inventory_tests inventory_tests.cpp)
target_link_libraries(designer_inventory_tests PRIVATE designer::inventory)
set_target_properties(designer_inventory_tests PROPERTIES CXX_EXTENSIONS OFF)
designer_enable_warnings(designer_inventory_tests)
add_test(NAME designer.inventory.rules COMMAND designer_inventory_tests)

foreach(test IN ITEMS inventory_transactions inventory_archive)
    add_executable(designer_${test}_tests ${test}_tests.cpp)
    target_link_libraries(designer_${test}_tests PRIVATE designer::inventory)
    set_target_properties(designer_${test}_tests PROPERTIES CXX_EXTENSIONS OFF)
    designer_enable_warnings(designer_${test}_tests)
    add_test(NAME designer.${test} COMMAND designer_${test}_tests)
endforeach()

if(DESIGNER_BUILD_EXAMPLES AND UNIX)
    add_test(NAME designer.storage_demo.save COMMAND designer_inventory_storage_demo
        ${CMAKE_CURRENT_BINARY_DIR}/demo-player.sav)
    set_tests_properties(designer.storage_demo.save PROPERTIES FIXTURES_SETUP saved_inventory)
    add_test(NAME designer.storage_demo.load COMMAND designer_inventory_storage_demo
        ${CMAKE_CURRENT_BINARY_DIR}/demo-player.sav --load)
    set_tests_properties(designer.storage_demo.load PROPERTIES FIXTURES_REQUIRED saved_inventory)
endif()
