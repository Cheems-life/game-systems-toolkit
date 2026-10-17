add_library(designer_economy STATIC src/economy.cpp src/archive.cpp)
add_library(designer::economy ALIAS designer_economy)
target_compile_features(designer_economy PUBLIC cxx_std_17)
set_target_properties(designer_economy PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(designer_economy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(designer_economy PUBLIC designer::inventory)
designer_enable_warnings(designer_economy)
