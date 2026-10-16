# The Editor's geometry on its own. The unit table and the transform promotion are not
# reachable from an instruction yet - only a loaded document carries a scale or a
# rotation - so they are checked here instead of through the pipeline.
add_executable(geometry_test geometry_test.cc)
target_include_directories(geometry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(geometry_test PRIVATE SvgEditor::Editor SvgEditor::Warnings)
add_test(NAME geometry COMMAND geometry_test)
