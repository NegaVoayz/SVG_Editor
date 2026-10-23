# The text front end. The library holds everything that is worth testing without a
# terminal - the instruction syntax, the value syntax and the printing - and the
# executable is the loop around it.
#
# One source per question the front end answers: the boundary and the command list in
# IntentParser.cc, the verbs and their readers in Command.cc, the element kinds `create`
# builds in Create.cc, the attributes `set` writes in Set.cc, the shape a reader is written
# in and what one word spells in Syntax.cc, and the other direction of the boundary in
# Rendering.cc and SvgValue.cc.
add_library(Ui
    src/IntentParser.cc
    src/Command.cc
    src/Create.cc
    src/Set.cc
    src/Syntax.cc
    src/Rendering.cc
    src/SvgValue.cc)
add_library(SvgEditor::Ui ALIAS Ui)

target_include_directories(Ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_link_libraries(Ui
    PUBLIC
        SvgEditor::Protocol   # the vocabulary it produces
        SvgEditor::Model      # the values that vocabulary is made of
    PRIVATE
        SvgEditor::Warnings)

add_executable(Cli src/main.cc)
set_target_properties(Cli PROPERTIES OUTPUT_NAME svg-editor)
target_compile_definitions(Cli PRIVATE SVG_EDITOR_VERSION="${PROJECT_VERSION}")

target_link_libraries(Cli
    PRIVATE
        SvgEditor::Ui
        SvgEditor::Planner
        SvgEditor::FileSystem
        SvgEditor::Model
        SvgEditor::Warnings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
