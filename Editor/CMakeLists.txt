# The Editor is the write path: it turns a change intent into one tree-level
# transaction. It reaches Model's transaction API and nothing else - the hive is
# not visible here.
add_library(Editor
    src/Editor.cc
    src/Geometry.cc)
add_library(SvgEditor::Editor ALIAS Editor)

target_include_directories(Editor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(Editor
    PUBLIC
        SvgEditor::Model
        SvgEditor::Protocol
    PRIVATE
        SvgEditor::Warnings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
