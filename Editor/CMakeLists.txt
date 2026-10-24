# The Editor is the write path: it turns a change intent into one tree-level
# transaction. It reaches Model's transaction API and nothing else - the hive is
# not visible here.
#
# One source per thing an edit changes, so that what an instruction does is one file to
# look in: the dispatch in Editor.cc, the elements a command builds in Create.cc, where a
# node sits in Structure.cc, what a node holds in Payload.cc, one named attribute in
# Set.cc, and the history in History.cc. Translations.hh is what holds them together, and it declares rather than
# defines: a specialization has to be seen before the visitor asks for it.
#
# The arithmetic underneath all of that is two sources of its own, because each of them
# answers a different question: what a length is worth (Units.cc), and what a move or a
# scale does to one node (Geometry.cc).
add_library(Editor
    src/Editor.cc
    src/Create.cc
    src/Structure.cc
    src/Payload.cc
    src/Set.cc
    src/History.cc
    src/Naming.cc
    src/Geometry.cc)
add_library(SvgEditor::Editor ALIAS Editor)

target_include_directories(Editor PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# Debug object files for Editor.cc exceed the default COFF section limit when the
# history outcome is stored inline. Big-object mode is the GNU MinGW assembler's
# supported representation for that case; Clang and non-Windows toolchains do not use it.
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(Editor PRIVATE -Wa,-mbig-obj)
endif()

target_link_libraries(Editor
    PUBLIC
        SvgEditor::Model
        SvgEditor::Protocol
    PRIVATE
        SvgEditor::Warnings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
