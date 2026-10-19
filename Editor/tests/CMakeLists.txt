# The Editor's param arithmetic on its own. Transform chains are checked beside it, from
# the Model transaction side they actually use.
add_executable(geometry_test geometry_test.cc)
target_include_directories(geometry_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(geometry_test PRIVATE SvgEditor::Editor SvgEditor::Warnings)
add_test(NAME geometry COMMAND geometry_test)

add_executable(transform_chain_test transform_chain_test.cc)
target_link_libraries(transform_chain_test PRIVATE SvgEditor::Editor SvgEditor::Warnings)
add_test(NAME transform_chain COMMAND transform_chain_test)
