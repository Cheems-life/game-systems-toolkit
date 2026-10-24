if(TARGET designer_game AND UNIX)
    add_test(NAME designer.application.full_flow
        COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:designer_game>
        -DCONTENT=${PROJECT_SOURCE_DIR}/apps/console/content
        -DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/console-flow
        -P ${CMAKE_CURRENT_SOURCE_DIR}/full_flow.cmake)
    set_tests_properties(designer.application.full_flow PROPERTIES TIMEOUT 60)
endif()
