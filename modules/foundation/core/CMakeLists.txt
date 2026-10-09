add_library(designer_core STATIC src/version.cpp)
add_library(designer::core ALIAS designer_core)
target_compile_features(designer_core PUBLIC cxx_std_17)
set_target_properties(designer_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
target_include_directories(designer_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
designer_enable_warnings(designer_core)
