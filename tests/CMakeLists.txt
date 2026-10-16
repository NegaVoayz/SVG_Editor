# The integration test drives the pipeline the way a front end does: one typed
# intent goes in at the Planner and is served by Model or by the Editor. Parsing is
# not in here - that is the text front end's, and it has its own test.
add_executable(pipeline_test pipeline_test.cc)
target_link_libraries(pipeline_test PRIVATE SvgEditor::Planner SvgEditor::Warnings)
# the "open a file that does exist" check needs the source tree by name, and the
# build directory sits at a depth the test should not have to know about
target_compile_definitions(pipeline_test PRIVATE SVG_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME pipeline COMMAND pipeline_test)

# the rule the target graph no longer expresses: no module outside Model/ may use
# the hive, so the sources are read rather than the link lines
add_test(NAME layering
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckLayering.cmake)

# The cli on its own: every run starts from a fresh document, so a chain of
# instructions has to be handed over in one argument.
add_test(NAME cli_tree COMMAND $<TARGET_FILE:Cli> "tree")
set_tests_properties(cli_tree PROPERTIES PASS_REGULAR_EXPRESSION "svg")

add_test(NAME cli_chain COMMAND $<TARGET_FILE:Cli> "create g layer; tree")
set_tests_properties(cli_chain PROPERTIES PASS_REGULAR_EXPRESSION "id=\"layer\"")

add_test(NAME cli_shape
    COMMAND $<TARGET_FILE:Cli> "create g layer; create rect box 0 0 10 10 #ff0000; addto layer box; move box 5 5; tree")
set_tests_properties(cli_shape PROPERTIES PASS_REGULAR_EXPRESSION "x=\"5\" y=\"5\"")

add_test(NAME cli_unknown_verb COMMAND $<TARGET_FILE:Cli> "frobnicate")
set_tests_properties(cli_unknown_verb PROPERTIES WILL_FAIL TRUE)
