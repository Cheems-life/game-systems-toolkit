add_library(designer_inventory STATIC src/inventory.cpp src/archive.cpp)
add_library(designer::inventory ALIAS designer_inventory)
target_compile_features(designer_inventory PUBLIC cxx_std_17)
set_target_properties(designer_inventory PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(designer_inventory PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
designer_enable_warnings(designer_inventory)
