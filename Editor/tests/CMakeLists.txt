# The Editor's param arithmetic on its own. Transform matrices are checked beside it, from
# the Model transaction side they actually use.
add_executable(geometry_test geometry_test.cc)
target_include_directories(geometry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(geometry_test PRIVATE SvgEditor::Editor SvgEditor::Warnings)
add_test(NAME geometry COMMAND geometry_test)

add_executable(transform_matrix_test transform_matrix_test.cc)
target_link_libraries(transform_matrix_test PRIVATE SvgEditor::Editor SvgEditor::Warnings)
add_test(NAME transform_matrix COMMAND transform_matrix_test)
