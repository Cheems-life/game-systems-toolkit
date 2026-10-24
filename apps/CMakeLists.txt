add_executable(designer_game console/main.cpp console/application.cpp)
target_link_libraries(designer_game PRIVATE designer::player designer::configuration
    designer::dialogue_configuration designer::persistence)
target_compile_definitions(designer_game PRIVATE DESIGNER_APP_CONTENT="${CMAKE_CURRENT_SOURCE_DIR}/console/content")
set_target_properties(designer_game PROPERTIES CXX_EXTENSIONS OFF RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
designer_enable_warnings(designer_game)
