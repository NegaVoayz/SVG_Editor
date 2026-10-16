# The parser on its own: the syntax is the front end's, so this is where the
# spelling of a length, a fill and an instruction is checked.
add_executable(intent_parser_test intent_parser_test.cc)
target_link_libraries(intent_parser_test PRIVATE SvgEditor::Ui SvgEditor::Warnings)
add_test(NAME intent_parser COMMAND intent_parser_test)
