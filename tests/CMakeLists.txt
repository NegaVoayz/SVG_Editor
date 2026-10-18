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
# MVCCHive, so the sources are read rather than the link lines
add_test(NAME layering
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -P ${CMAKE_SOURCE_DIR}/cmake/CheckLayering.cmake)

# The cli on its own: every run starts without a document, so `init` is the first
# instruction of a session and a chain of instructions is handed over in one argument.
add_test(NAME cli_no_document COMMAND $<TARGET_FILE:Cli> "tree")
set_tests_properties(cli_no_document PROPERTIES PASS_REGULAR_EXPRESSION
    "error: no document. use 'init' or 'read' first")

add_test(NAME cli_tree COMMAND $<TARGET_FILE:Cli> "init; tree")
set_tests_properties(cli_tree PROPERTIES PASS_REGULAR_EXPRESSION "svg .800x600.")

add_test(NAME cli_chain COMMAND $<TARGET_FILE:Cli> "init; create g layer; tree")
set_tests_properties(cli_chain PROPERTIES PASS_REGULAR_EXPRESSION "g#layer")

add_test(NAME cli_shape
    COMMAND $<TARGET_FILE:Cli> "init; create g layer; create rect box 0 0 10 10 #ff0000; addto layer box; move box 5 5; tree")
set_tests_properties(cli_shape PROPERTIES PASS_REGULAR_EXPRESSION "└── rect#box +x=5 y=5 w=10 h=10 fill=#ff0000")

# `set` names one attribute by its spelling and leaves the rest of the line to the value
# syntax that name implies
add_test(NAME cli_set
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 10 10; set box x 5; set box fill #00ff00; set box stroke-width 2; tree")
set_tests_properties(cli_set PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#box +x=5 y=0 w=10 h=10 fill=#00ff00 stroke-width=2")

# an attribute the node does not carry is refused with its own spelling
add_test(NAME cli_unknown_attribute COMMAND $<TARGET_FILE:Cli> "init; create circle dot 1 1 1; set dot x 5")
set_tests_properties(cli_unknown_attribute PROPERTIES PASS_REGULAR_EXPRESSION "error: unknown attribute: x")

# the drawing is what a text element's content is printed in the line of, and the line
# under a last child hangs from spaces rather than bars
add_test(NAME cli_text COMMAND $<TARGET_FILE:Cli> "init; create text title 20 40 \"My Diagram\"; tree")
set_tests_properties(cli_text PROPERTIES PASS_REGULAR_EXPRESSION "\\(20,40\\) \"My Diagram\"")

add_test(NAME cli_nested COMMAND $<TARGET_FILE:Cli>
    "init; create g layer; create rect box 0 0 10 10 in layer; create circle dot 1 1 1 in layer; tree")
set_tests_properties(cli_nested PROPERTIES PASS_REGULAR_EXPRESSION
    "    ├── rect#box +x=0 y=0 w=10 h=10\n    └── circle#dot +cx=1 cy=1 r=1")

# The unit systems are never mixed: an inch does not add up with a centimetre, and the
# answer says which of the two is which. WILL_FAIL cannot stand next to a pattern here -
# ctest inverts the pattern when it is set - and the exit code of a refused instruction is
# what the unknown-verb case below covers.
add_test(NAME cli_units COMMAND $<TARGET_FILE:Cli> "init; create rect r 1cm 0 10mm 10mm; move r 1in 0")
set_tests_properties(cli_units PROPERTIES PASS_REGULAR_EXPRESSION "1in is imperial and 1cm is metric")

add_test(NAME cli_help COMMAND $<TARGET_FILE:Cli> "help")
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "create <kind> <id>")

add_test(NAME cli_unknown_verb COMMAND $<TARGET_FILE:Cli> "frobnicate")
set_tests_properties(cli_unknown_verb PROPERTIES WILL_FAIL TRUE)
