# 商城测试：规则、存档格式和隔离的分配失败注入；fixture 先保存再启动加载进程。
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
