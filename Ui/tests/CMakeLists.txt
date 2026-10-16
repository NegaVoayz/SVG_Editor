# The parser on its own: the syntax is the front end's, so this is where the
# spelling of a length, a fill and an instruction is checked.
add_executable(intent_parser_test intent_parser_test.cc)
# The check that the help names every verb reads the table of verbs, and that table is the
# front end's own source rather than anything it offers the layers above.
target_include_directories(intent_parser_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(intent_parser_test PRIVATE SvgEditor::Ui SvgEditor::Warnings)
add_test(NAME intent_parser COMMAND intent_parser_test)

# The other direction of the same boundary, without a terminal or a document in the way:
# a view of rows in, the lines a user reads out.
add_executable(rendering_test rendering_test.cc)
target_link_libraries(rendering_test PRIVATE SvgEditor::Ui SvgEditor::Warnings)
add_test(NAME rendering COMMAND rendering_test)
