# Model's own test. It links Model, so it can see whatever Model shows its
# consumers - the hive stays out of sight, which is the point of the layering.
add_executable(document_test document_test.cc)
target_link_libraries(document_test PRIVATE SvgEditor::Model SvgEditor::Warnings)
add_test(NAME document COMMAND document_test)
