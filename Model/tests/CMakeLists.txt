# Model's own test. It links Model, so it can see whatever Model shows its
# consumers - the hive stays out of sight, which is the point of the layering.
add_executable(document_test document_test.cc)
target_link_libraries(document_test PRIVATE SvgEditor::Model SvgEditor::Warnings)
add_test(NAME document COMMAND document_test)

# The file reader and writer exercise the private XML boundary too. This test is inside
# Model, so it may include Model/src while no other module can.
add_executable(svg_io_test svg_io_test.cc)
target_include_directories(svg_io_test PRIVATE ${CMAKE_SOURCE_DIR}/Model/src)
target_link_libraries(svg_io_test PRIVATE SvgEditor::Model SvgEditor::Warnings)
add_test(NAME svg_io COMMAND svg_io_test)
