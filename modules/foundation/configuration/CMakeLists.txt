# 四个领域目录的启动配置适配器；领域库不依赖本模块，JSON 仅为私有实现。
add_library(designer_configuration STATIC src/bundle.cpp)
add_library(designer::configuration ALIAS designer_configuration)
target_compile_features(designer_configuration PUBLIC cxx_std_17)
target_include_directories(designer_configuration PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_include_directories(designer_configuration SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/nlohmann_json/include)
target_link_libraries(designer_configuration PUBLIC designer::quest_core designer::commerce designer::interaction PRIVATE designer::persistence)
set_target_properties(designer_configuration PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
designer_enable_warnings(designer_configuration)
