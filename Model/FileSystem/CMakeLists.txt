# The file layer under Model: it moves bytes in and out of the document's file
# and knows nothing about the tree. The Planner's context changes end up here,
# through Document::open()/save(), so no other module has to know where a
# document is stored.
add_library(FileSystem src/FileSystem.cc)
add_library(SvgEditor::FileSystem ALIAS FileSystem)

target_include_directories(FileSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(FileSystem PRIVATE cxx_std_23)
target_link_libraries(FileSystem PRIVATE SvgEditor::Warnings)
