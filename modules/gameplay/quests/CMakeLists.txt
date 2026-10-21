# The task core does not link commerce; the player coordinator composes both business modules.
add_library(designer_quest_core STATIC src/objectives.cpp src/quests.cpp src/facts.cpp)
add_library(designer::quest_core ALIAS designer_quest_core)
target_compile_features(designer_quest_core PUBLIC cxx_std_17)
target_include_directories(designer_quest_core PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_link_libraries(designer_quest_core PUBLIC designer::economy)
set_target_properties(designer_quest_core PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)
designer_enable_warnings(designer_quest_core)

# Both task targets expose only the task core, never the player coordinator.
add_library(designer::quests ALIAS designer_quest_core)
