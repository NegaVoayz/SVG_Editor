# The Planner is the only module that decides what an instruction means, so it is
# the only one that routes to both write paths: Editor and FileSystem.
add_library(Planner src/Planner.cc)
add_library(SvgEditor::Planner ALIAS Planner)

target_include_directories(Planner PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(Planner
    PUBLIC
        SvgEditor::Model
        SvgEditor::Editor
        SvgEditor::FileSystem
        SvgEditor::Protocol
    PRIVATE
        SvgEditor::Warnings)
