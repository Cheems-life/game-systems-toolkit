add_library(designer_commerce STATIC src/shop.cpp src/archive.cpp)
add_library(designer::commerce ALIAS designer_commerce)
target_compile_features(designer_commerce PUBLIC cxx_std_17)
set_target_properties(designer_commerce PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(designer_commerce PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(designer_commerce PUBLIC designer::economy)
designer_enable_warnings(designer_commerce)
