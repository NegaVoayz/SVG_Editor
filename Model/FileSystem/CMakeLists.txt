# The file layer under Model: it moves bytes and turns the format-neutral SvgNode tree
# into SVG lines. Model interprets the document tree and supplies that values-only form;
# this layer owns escaping, indentation and element syntax. The Planner's context changes
# arrive here through Document::open()/save().
add_library(FileSystem src/FileSystem.cc)
add_library(SvgEditor::FileSystem ALIAS FileSystem)

target_include_directories(FileSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(FileSystem PRIVATE cxx_std_23)
target_link_libraries(FileSystem PRIVATE SvgEditor::Warnings)

if(BUILD_TESTING)
    add_executable(file_system_test tests/FileSystem_test.cc)
    target_link_libraries(file_system_test PRIVATE FileSystem SvgEditor::Warnings)
    add_test(NAME file_system COMMAND file_system_test)
endif()
