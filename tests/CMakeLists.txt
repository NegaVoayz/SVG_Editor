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

# `show` names the node's parent on the heading line, not only its own attributes.
add_test(NAME cli_show_parent
    COMMAND $<TARGET_FILE:Cli> "init; create g markers; create circle c1 100 100 20 red in markers; show c1")
set_tests_properties(cli_show_parent PROPERTIES PASS_REGULAR_EXPRESSION "circle#c1  parent=g#markers")

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

# `text` is the UI attribute for the PlainText child of a text element.
add_test(NAME cli_set_text
    COMMAND $<TARGET_FILE:Cli> "init; create text title 20 40 \"My Diagram\"; set title text \"My Diagram v2\"; tree")
set_tests_properties(cli_set_text PROPERTIES PASS_REGULAR_EXPRESSION "\"My Diagram v2\"")

# Deleting a leaf does not claim a subtree went with it, and edits have no route label.
add_test(NAME cli_delete_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 1 1; delete box")
set_tests_properties(cli_delete_leaf PROPERTIES
    PASS_REGULAR_EXPRESSION "deleted rect#box"
    FAIL_REGULAR_EXPRESSION "under it|\\[change\\]")

# Character data is not a user-visible node: deleting a text element that holds only
# its words is still a leaf delete.
add_test(NAME cli_delete_text_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create text title 0 0 \"hello\"; delete title")
set_tests_properties(cli_delete_text_leaf PROPERTIES
    PASS_REGULAR_EXPRESSION "deleted text#title"
    FAIL_REGULAR_EXPRESSION "under it")

# In mixed content only the element child counts; the words on either side of it are
# part of the text element, not nodes of their own.
add_test(NAME cli_delete_mixed_text
    COMMAND $<TARGET_FILE:Cli>
        "read ${CMAKE_SOURCE_DIR}/tests/fixtures/mixed-text.svg; delete mixed")
set_tests_properties(cli_delete_mixed_text PROPERTIES
    PASS_REGULAR_EXPRESSION "and 1 node\\(s\\) under it"
    FAIL_REGULAR_EXPRESSION "and 2 node\\(s\\) under it")

# A handle is the hive's internal id, so the front end leaves '#' to the SVG id syntax.
add_test(NAME cli_tree_hash_is_an_id
    COMMAND $<TARGET_FILE:Cli> "init; create text t1 5 6 \"hello\"; tree #2")
set_tests_properties(cli_tree_hash_is_an_id PROPERTIES
    PASS_REGULAR_EXPRESSION "error: no such shape: #2")

# The root is the document's svg, so both '/' and 'svg' name it. An exact id still wins.
add_test(NAME cli_root_by_element_name
    COMMAND $<TARGET_FILE:Cli> "init; create rect a 0 0 1 1 in svg; tree")
set_tests_properties(cli_root_by_element_name PROPERTIES PASS_REGULAR_EXPRESSION "rect#a")

# Grouping the same node twice would make the reported member count a lie.
add_test(NAME cli_group_duplicate
    COMMAND $<TARGET_FILE:Cli> "init; create rect a 0 0 1 1; group g a a")
set_tests_properties(cli_group_duplicate PROPERTIES
    PASS_REGULAR_EXPRESSION "error: duplicate node in group: a")

# Event answers name elements the way the tree does; the hive's numeric handle never
# appears in one.
add_test(NAME cli_no_internal_handles
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r1 0 0 1 1 in g1; create circle c1 0 0 1 in g1; move r1 5 5; set r1 fill red; front r1; back r1; addto g1 r1")
set_tests_properties(cli_no_internal_handles PROPERTIES
    PASS_REGULAR_EXPRESSION
        "moved rect#r1 by 5 5\nset fill on rect#r1\nmoved rect#r1 to the front of g#g1\nmoved rect#r1 to the back of g#g1\nmoved rect#r1 to the end of g#g1"
    FAIL_REGULAR_EXPRESSION "#0|#[1-9][0-9]*")

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

# A canvas needs a positive size, and a shape dimension cannot be negative.
add_test(NAME cli_negative_init COMMAND $<TARGET_FILE:Cli> "init -5 -5")
set_tests_properties(cli_negative_init PROPERTIES PASS_REGULAR_EXPRESSION
    "error: canvas width must be positive")

add_test(NAME cli_zero_canvas COMMAND $<TARGET_FILE:Cli> "init; canvas 0")
set_tests_properties(cli_zero_canvas PROPERTIES PASS_REGULAR_EXPRESSION
    "error: canvas width must be positive")

add_test(NAME cli_negative_width
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 -1 1")
set_tests_properties(cli_negative_width PROPERTIES PASS_REGULAR_EXPRESSION
    "error: width must not be negative")

add_test(NAME cli_negative_set_width
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; set r width -1")
set_tests_properties(cli_negative_set_width PROPERTIES PASS_REGULAR_EXPRESSION
    "error: width must not be negative")

# A refused set leaves the tree untouched, including when it is saved and read back.
add_test(NAME cli_negative_set_width_not_saved
    COMMAND $<TARGET_FILE:Cli>
        "init; create rect a 0 0 10 10; set a width -7; save ${CMAKE_CURRENT_BINARY_DIR}/negative_set.svg; read ${CMAKE_CURRENT_BINARY_DIR}/negative_set.svg; show a")
set_tests_properties(cli_negative_set_width_not_saved PROPERTIES
    PASS_REGULAR_EXPRESSION "w=10 h=10"
    FAIL_REGULAR_EXPRESSION "w=-7")

add_test(NAME cli_undo_after_init COMMAND $<TARGET_FILE:Cli> "init; undo")
set_tests_properties(cli_undo_after_init PROPERTIES PASS_REGULAR_EXPRESSION
    "error: undo not allowed after io command")

add_test(NAME cli_undo_after_save COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; save ${CMAKE_CURRENT_BINARY_DIR}/undo_after_save.svg; undo")
set_tests_properties(cli_undo_after_save PROPERTIES PASS_REGULAR_EXPRESSION
    "error: undo not allowed after io command")

add_test(NAME cli_undo_to_edit_baseline
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; undo; undo")
set_tests_properties(cli_undo_to_edit_baseline PROPERTIES PASS_REGULAR_EXPRESSION
    "error: undo not allowed after io command")

add_test(NAME cli_undo_after_canvas COMMAND $<TARGET_FILE:Cli> "init; canvas 1024 768; undo")
set_tests_properties(cli_undo_after_canvas PROPERTIES PASS_REGULAR_EXPRESSION
    "error: undo not allowed after io command")

# A parameter with a space is one word when it is quoted, and the answer prints it back
# the same way so the reader can copy it into another instruction.
add_test(NAME cli_quoted_id COMMAND $<TARGET_FILE:Cli>
    "init; create g \"my layer\"; create rect \"my box\" 0 0 10 10 in \"my layer\"; tree")
set_tests_properties(cli_quoted_id PROPERTIES PASS_REGULAR_EXPRESSION "g#my layer")

add_test(NAME cli_quoted_id_error COMMAND $<TARGET_FILE:Cli> "init; delete \"id with space\"")
set_tests_properties(cli_quoted_id_error PROPERTIES PASS_REGULAR_EXPRESSION "error: no such shape: \"id with space\"")

add_test(NAME cli_quoted_path COMMAND $<TARGET_FILE:Cli> "read \"no such file.svg\"")
set_tests_properties(cli_quoted_path PROPERTIES PASS_REGULAR_EXPRESSION "error: cannot read file: \"no such file.svg\"")

# File input reports the ids it had to invent, and the tree is the loaded document.
add_test(NAME cli_read
    COMMAND $<TARGET_FILE:Cli> "read ${CMAKE_SOURCE_DIR}/tests/fixtures/load.svg; tree")
set_tests_properties(cli_read PROPERTIES PASS_REGULAR_EXPRESSION
    "warning: assigned id rect1 to unnamed <rect>")

add_test(NAME cli_read_count
    COMMAND $<TARGET_FILE:Cli> "read ${CMAKE_SOURCE_DIR}/tests/fixtures/load.svg")
set_tests_properties(cli_read_count PROPERTIES PASS_REGULAR_EXPRESSION
    "read .* \\(12 elements\\)")

add_test(NAME cli_read_tree
    COMMAND $<TARGET_FILE:Cli> "read ${CMAKE_SOURCE_DIR}/tests/fixtures/load.svg; tree")
set_tests_properties(cli_read_tree PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#rect1 +x=1 y=2 w=30 h=40 fill=#3366ff")

# A save writes the current model tree, and reading it back preserves the named paints
# and the ids that the first read assigned.
add_test(NAME cli_save_roundtrip
    COMMAND $<TARGET_FILE:Cli>
        "read ${CMAKE_SOURCE_DIR}/tests/fixtures/load.svg; save ${CMAKE_CURRENT_BINARY_DIR}/cli_roundtrip.svg; read ${CMAKE_CURRENT_BINARY_DIR}/cli_roundtrip.svg; tree")
set_tests_properties(cli_save_roundtrip PROPERTIES PASS_REGULAR_EXPRESSION "fill=rebeccapurple")

add_test(NAME cli_save_count COMMAND $<TARGET_FILE:Cli>
    "init; create rect only 0 0 1 1; save ${CMAKE_CURRENT_BINARY_DIR}/cli_count.svg")
set_tests_properties(cli_save_count PROPERTIES PASS_REGULAR_EXPRESSION
    "saved to .*cli_count\\.svg \\(1 element\\)")

# A comment runs to the end of the line, including semicolons inside it.
add_test(NAME cli_comment COMMAND $<TARGET_FILE:Cli> "# a note; with an init that must not run")

# A create reports the element by the name a tree would print it by, and the parent by
# the same kind#id pair.
add_test(NAME cli_create_default COMMAND $<TARGET_FILE:Cli> "init; create g node1")
set_tests_properties(cli_create_default PROPERTIES PASS_REGULAR_EXPRESSION "g#node1 created")

add_test(NAME cli_create_result COMMAND $<TARGET_FILE:Cli>
    "init; create g node1; create text start-label 0 0 \"hello\" in node1")
set_tests_properties(cli_create_result PROPERTIES PASS_REGULAR_EXPRESSION
    "text#start-label created in g#node1")

add_test(NAME cli_help COMMAND $<TARGET_FILE:Cli> "help")
set_tests_properties(cli_help PROPERTIES PASS_REGULAR_EXPRESSION "create <kind> <id>")

add_test(NAME cli_unknown_verb COMMAND $<TARGET_FILE:Cli> "frobnicate")
set_tests_properties(cli_unknown_verb PROPERTIES WILL_FAIL TRUE)
