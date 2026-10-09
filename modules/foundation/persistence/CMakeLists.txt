add_library(designer_persistence STATIC src/file_store.cpp)
add_library(designer::persistence ALIAS designer_persistence)
target_compile_features(designer_persistence PUBLIC cxx_std_17)
set_target_properties(designer_persistence PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(designer_persistence PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
designer_enable_warnings(designer_persistence)
