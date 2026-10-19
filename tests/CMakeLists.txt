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
set_tests_properties(cli_tree PROPERTIES
    PASS_REGULAR_EXPRESSION "svg .800x600."
    FAIL_REGULAR_EXPRESSION "\\[(view|context|unknown|parse|failed|change)\\]")

add_test(NAME cli_chain COMMAND $<TARGET_FILE:Cli> "init; create g layer; tree")
set_tests_properties(cli_chain PROPERTIES PASS_REGULAR_EXPRESSION "g#layer")

add_test(NAME cli_shape
    COMMAND $<TARGET_FILE:Cli>
        "init; create g layer; create rect box 0 0 10 10 #ff0000; addto layer box; move box 5 5 params restrict; tree")
set_tests_properties(cli_shape PROPERTIES PASS_REGULAR_EXPRESSION "└── rect#box +x=5 y=5 w=10 h=10 fill=#ff0000")

# `move` and `scale` can change a leaf's transform, a leaf's own params, or a
# container's descendants' params. The mismatched combinations are refused.
add_test(NAME cli_move_transform
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 10 10; move r 5 0 transform; show r")
set_tests_properties(cli_move_transform PROPERTIES PASS_REGULAR_EXPRESSION "transform=translate\\(5 0\\)")

add_test(NAME cli_move_restrict_params
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 10 10; move r 5 0 params restrict; show r")
set_tests_properties(cli_move_restrict_params PROPERTIES PASS_REGULAR_EXPRESSION "x=5 y=0 w=10 h=10")

add_test(NAME cli_move_cascade_rect
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r 0 0 10 10 in g1; move g1 2 3 cascade; tree")
set_tests_properties(cli_move_cascade_rect PROPERTIES PASS_REGULAR_EXPRESSION "x=2 y=3 w=10 h=10")

add_test(NAME cli_move_cascade_text
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create text t 5 6 \"hi\" in g1; move g1 2 3; tree")
set_tests_properties(cli_move_cascade_text PROPERTIES PASS_REGULAR_EXPRESSION "\\(7,9\\) text=\"hi\"")

add_test(NAME cli_move_cascade_text_children COMMAND $<TARGET_FILE:Cli>
    "read ${CMAKE_SOURCE_DIR}/tests/fixtures/mixed-text.svg; move svg 2 3; tree")
set_tests_properties(cli_move_cascade_text_children PROPERTIES PASS_REGULAR_EXPRESSION "\\(2,23\\)")

add_test(NAME cli_scale_cascade
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r 0 0 10 10 in g1; scale g1 2; tree")
set_tests_properties(cli_scale_cascade PROPERTIES PASS_REGULAR_EXPRESSION "x=0 y=0 w=20 h=20")

add_test(NAME cli_scale_transform
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 10 10; scale r 2 transform; show r")
set_tests_properties(cli_scale_transform PROPERTIES PASS_REGULAR_EXPRESSION "transform=scale\\(2 2\\)")

add_test(NAME cli_scale_result COMMAND $<TARGET_FILE:Cli>
    "init; create circle c1 10 10 5; scale c1 2 params restrict")
set_tests_properties(cli_scale_result PROPERTIES PASS_REGULAR_EXPRESSION "circle#c1 scaled by 2")

add_test(NAME cli_move_cascade_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; move r 1 1 cascade")
set_tests_properties(cli_move_cascade_leaf PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#r moved by \\(1, 1\\)")

add_test(NAME cli_move_default_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; move r 1 1")
set_tests_properties(cli_move_default_leaf PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#r moved by \\(1, 1\\)")

add_test(NAME cli_move_restrict_container
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r 0 0 1 1 in g1; move g1 0 100 restrict")
set_tests_properties(cli_move_restrict_container PROPERTIES PASS_REGULAR_EXPRESSION
    "g#g1 moved by \\(0, 100\\)")

add_test(NAME cli_move_empty_container
    COMMAND $<TARGET_FILE:Cli> "init; create g g1; move g1 1 1 restrict")
set_tests_properties(cli_move_empty_container PROPERTIES PASS_REGULAR_EXPRESSION
    "error: cannot move or scale an empty group: g#g1")

add_test(NAME cli_move_transform_container
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r 0 0 1 1 in g1; move g1 0 100 transform; show g1")
set_tests_properties(cli_move_transform_container PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=translate\\(0 100\\)")

# A node that already has a transform is a coordinate-system boundary: the default move
# belongs in front of that transform, in the parent's space, not inside the rotated params.
add_test(NAME cli_move_transformed_container_default
    COMMAND $<TARGET_FILE:Cli>
        "init; create g gR; create rect a 0 0 10 10 in gR; set gR transform \"rotate(90)\"; move gR 10 0; show gR")
set_tests_properties(cli_move_transformed_container_default PROPERTIES PASS_REGULAR_EXPRESSION
    "g#gR moved by \\(10, 0\\)")

add_test(NAME cli_move_transformed_container_chain
    COMMAND $<TARGET_FILE:Cli>
        "init; create g gR; create rect a 0 0 10 10 in gR; set gR transform \"rotate(90)\"; move gR 10 0; show gR")
set_tests_properties(cli_move_transformed_container_chain PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=translate\\(10 0\\) rotate\\(90\\)")

add_test(NAME cli_move_transformed_container_params
    COMMAND $<TARGET_FILE:Cli>
        "init; create g gR; create rect a 0 0 10 10 in gR; set gR transform \"rotate(90)\"; move gR 10 0 params cascade; show a")
set_tests_properties(cli_move_transformed_container_params PROPERTIES PASS_REGULAR_EXPRESSION
    "x=0 y=0 w=10 h=10")

add_test(NAME cli_scale_transform_container
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r 0 0 1 1 in g1; scale g1 2 transform; show g1")
set_tests_properties(cli_scale_transform_container PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=scale\\(2 2\\)")

add_test(NAME cli_move_transform_scope
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; move r 1 1 transform cascade")
set_tests_properties(cli_move_transform_scope PROPERTIES PASS_REGULAR_EXPRESSION
    "error: transform does not take cascade or restrict")

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

add_test(NAME cli_set_result COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; set r fill red; set r class \"layer one\"")
set_tests_properties(cli_set_result PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#r updated: fill=red\nrect#r updated: class=\"layer one\"")

add_test(NAME cli_set_transform_rotate_center
    COMMAND $<TARGET_FILE:Cli>
        "init; create rect box 0 0 10 10; set box transform \"rotate(30 10 20)\"; show box")
set_tests_properties(cli_set_transform_rotate_center PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=translate\\(11.3397 -2.32051\\) rotate\\(30\\)")

add_test(NAME cli_set_transform_skew
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 10 10; set box transform \"skewX(15)\"; show box")
set_tests_properties(cli_set_transform_skew PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=skewX\\(15\\)")

add_test(NAME cli_set_transform_skew_y
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 10 10; set box transform \"skewY(15)\"; show box")
set_tests_properties(cli_set_transform_skew_y PROPERTIES PASS_REGULAR_EXPRESSION
    "transform=skewY\\(15\\)")

add_test(NAME cli_set_transform_result
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 10 10; set box transform \"skewY(15)\"")
set_tests_properties(cli_set_transform_result PROPERTIES PASS_REGULAR_EXPRESSION
    "rect#box updated: transform=skewY\\(15\\)")

add_test(NAME cli_set_transform_matrix
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 10 10; set box transform \"matrix(1 0 0 1 5 6)\"; show box")
set_tests_properties(cli_set_transform_matrix PROPERTIES PASS_REGULAR_EXPRESSION "transform=translate\\(5 6\\)")

# an attribute the node does not carry is refused with its own spelling
add_test(NAME cli_unknown_attribute COMMAND $<TARGET_FILE:Cli> "init; create circle dot 1 1 1; set dot x 5")
set_tests_properties(cli_unknown_attribute PROPERTIES PASS_REGULAR_EXPRESSION "error: unknown attribute: x")

# the drawing is what a text element's content is printed in the line of, and the line
# under a last child hangs from spaces rather than bars
add_test(NAME cli_text COMMAND $<TARGET_FILE:Cli> "init; create text title 20 40 \"My Diagram\"; tree")
set_tests_properties(cli_text PROPERTIES PASS_REGULAR_EXPRESSION "\\(20,40\\) text=\"My Diagram\"")

# `text` is the UI attribute for the PlainText child of a text element.
add_test(NAME cli_set_text
    COMMAND $<TARGET_FILE:Cli> "init; create text title 20 40 \"My Diagram\"; set title text \"My Diagram v2\"; tree")
set_tests_properties(cli_set_text PROPERTIES PASS_REGULAR_EXPRESSION "text=\"My Diagram v2\"")

add_test(NAME cli_set_text_result COMMAND $<TARGET_FILE:Cli>
    "init; create text t 0 0 \"old\"; set t text \"Hello World\"")
set_tests_properties(cli_set_text_result PROPERTIES PASS_REGULAR_EXPRESSION
    "text#t updated: text=\"Hello World\"")

# The character data is printed after the text element's paint attributes.
add_test(NAME cli_text_content_last COMMAND $<TARGET_FILE:Cli>
    "init; create text t 20 40 \"hello\"; set t fill red; tree")
set_tests_properties(cli_text_content_last PROPERTIES PASS_REGULAR_EXPRESSION
    "\\(20,40\\) fill=red text=\"hello\"")

# Deleting a leaf does not claim a subtree went with it, and answers have no route label.
add_test(NAME cli_delete_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create rect box 0 0 1 1; delete box")
set_tests_properties(cli_delete_leaf PROPERTIES
    PASS_REGULAR_EXPRESSION "rect#box deleted"
    FAIL_REGULAR_EXPRESSION "under it|\\[[a-z]+\\]")

# Character data is not a user-visible node: deleting a text element that holds only
# its words is still a leaf delete.
add_test(NAME cli_delete_text_leaf
    COMMAND $<TARGET_FILE:Cli> "init; create text title 0 0 \"hello\"; delete title")
set_tests_properties(cli_delete_text_leaf PROPERTIES
    PASS_REGULAR_EXPRESSION "text#title deleted"
    FAIL_REGULAR_EXPRESSION "under it")

# In mixed content only the element child counts; the words on either side of it are
# part of the text element, not nodes of their own.
add_test(NAME cli_delete_mixed_text
    COMMAND $<TARGET_FILE:Cli>
        "read ${CMAKE_SOURCE_DIR}/tests/fixtures/mixed-text.svg; delete mixed")
set_tests_properties(cli_delete_mixed_text PROPERTIES
    PASS_REGULAR_EXPRESSION "text#mixed deleted \\(1 element\\)"
    FAIL_REGULAR_EXPRESSION "deleted \\(2 elements\\)|under it")

# A handle is the hive's internal id, so the front end leaves '#' to the SVG id syntax.
add_test(NAME cli_tree_hash_is_an_id
    COMMAND $<TARGET_FILE:Cli> "init; create text t1 5 6 \"hello\"; tree #2")
set_tests_properties(cli_tree_hash_is_an_id PROPERTIES
    PASS_REGULAR_EXPRESSION "error: no such shape: #2")

# The root is the document's svg, so both '/' and 'svg' name it. An exact id still wins.
add_test(NAME cli_root_by_element_name
    COMMAND $<TARGET_FILE:Cli> "init; create rect a 0 0 1 1 in svg; tree")
set_tests_properties(cli_root_by_element_name PROPERTIES PASS_REGULAR_EXPRESSION "rect#a")

# The root alias is reserved: `svg` is how the root is named, so no document may keep it
# as an element id.
add_test(NAME cli_reserved_root_id COMMAND $<TARGET_FILE:Cli> "init; create g svg")
set_tests_properties(cli_reserved_root_id PROPERTIES PASS_REGULAR_EXPRESSION
    "error: id \"svg\" is reserved for the root")

add_test(NAME cli_reserved_group_id COMMAND $<TARGET_FILE:Cli>
    "init; create rect a 0 0 1 1; group svg a")
set_tests_properties(cli_reserved_group_id PROPERTIES PASS_REGULAR_EXPRESSION
    "error: id \"svg\" is reserved for the root")

# A file may contain the reserved id; read renames it and says so.
add_test(NAME cli_read_reserved_id_warning COMMAND $<TARGET_FILE:Cli>
    "read ${CMAKE_SOURCE_DIR}/tests/fixtures/reserved-id.svg")
set_tests_properties(cli_read_reserved_id_warning PROPERTIES PASS_REGULAR_EXPRESSION
    "warning: id \"svg\" is reserved for the root, renamed <rect> to rect2")

add_test(NAME cli_read_reserved_id_tree COMMAND $<TARGET_FILE:Cli>
    "read ${CMAKE_SOURCE_DIR}/tests/fixtures/reserved-id.svg; tree")
set_tests_properties(cli_read_reserved_id_tree PROPERTIES PASS_REGULAR_EXPRESSION "g#g1")

# Grouping the same node twice would make the reported member count a lie.
add_test(NAME cli_group_duplicate
    COMMAND $<TARGET_FILE:Cli> "init; create rect a 0 0 1 1; group g a a")
set_tests_properties(cli_group_duplicate PROPERTIES
    PASS_REGULAR_EXPRESSION "error: duplicate node in group: a")

add_test(NAME cli_group_result COMMAND $<TARGET_FILE:Cli>
    "init; create rect a 0 0 1 1; create rect b 0 0 1 1; group gg a b")
set_tests_properties(cli_group_result PROPERTIES PASS_REGULAR_EXPRESSION
    "g#gg created \\(2 elements\\)")

add_test(NAME cli_ungroup_result COMMAND $<TARGET_FILE:Cli>
    "init; create g layer; create rect a 0 0 1 1 in layer; create rect b 0 0 1 1 in layer; group gg a b; ungroup gg")
set_tests_properties(cli_ungroup_result PROPERTIES PASS_REGULAR_EXPRESSION
    "g#gg ungrouped into g#layer \\(2 elements\\)")

add_test(NAME cli_addto_cycle_error COMMAND $<TARGET_FILE:Cli>
    "init; create g layer; create g inner in layer; addto inner layer")
set_tests_properties(cli_addto_cycle_error PROPERTIES PASS_REGULAR_EXPRESSION
    "error: cannot add a container to its own descendant: layer")

# Event answers name elements the way the tree does; the hive's numeric handle never
# appears in one.
add_test(NAME cli_no_internal_handles
    COMMAND $<TARGET_FILE:Cli>
        "init; create g g1; create rect r1 0 0 1 1 in g1; create circle c1 0 0 1 in g1; move r1 5 5 params restrict; set r1 fill red; front r1; back r1; addto g1 r1")
set_tests_properties(cli_no_internal_handles PROPERTIES
    PASS_REGULAR_EXPRESSION
        "rect#r1 moved by \\(5, 5\\)\nrect#r1 updated: fill=red\nrect#r1 moved to the front of g#g1\nrect#r1 moved to the back of g#g1\nrect#r1 moved to the end of g#g1"
    FAIL_REGULAR_EXPRESSION "#0|#[1-9][0-9]*")

add_test(NAME cli_nested COMMAND $<TARGET_FILE:Cli>
    "init; create g layer; create rect box 0 0 10 10 in layer; create circle dot 1 1 1 in layer; tree")
set_tests_properties(cli_nested PROPERTIES PASS_REGULAR_EXPRESSION
    "    ├── rect#box +x=0 y=0 w=10 h=10\n    └── circle#dot +cx=1 cy=1 r=1")

# The unit systems are never mixed: an inch does not add up with a centimetre, and the
# answer says which of the two is which. WILL_FAIL cannot stand next to a pattern here -
# ctest inverts the pattern when it is set - and the exit code of a refused instruction is
# what the unknown-verb case below covers.
add_test(NAME cli_units
    COMMAND $<TARGET_FILE:Cli> "init; create rect r 1cm 0 10mm 10mm; move r 1in 0 params restrict")
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

add_test(NAME cli_nan_length COMMAND $<TARGET_FILE:Cli> "init; create rect nn nan 0 1 1")
set_tests_properties(cli_nan_length PROPERTIES PASS_REGULAR_EXPRESSION
    "cannot read 'nan' as a length")

add_test(NAME cli_scale_overflow
    COMMAND $<TARGET_FILE:Cli> "init; create rect huge 3e38 0 3e38 1; scale huge 2 params restrict; show huge")
set_tests_properties(cli_scale_overflow PROPERTIES
    PASS_REGULAR_EXPRESSION "error: \"scaled value\" must be a finite number"
    FAIL_REGULAR_EXPRESSION "x=inf|w=inf")

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

add_test(NAME cli_undo_redo_history_outcome COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; move r 5 7; undo; redo")
set_tests_properties(cli_undo_redo_history_outcome PROPERTIES PASS_REGULAR_EXPRESSION
    "undo: rect#r moved by \\(5, 7\\)\nredo: rect#r moved by \\(5, 7\\)")

# `history` lists the current context branch, and `checkout` takes one of its ids.
add_test(NAME cli_history COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; move r 5 7; history")
set_tests_properties(cli_history PROPERTIES PASS_REGULAR_EXPRESSION
    "2 +rect#r created\n3 +rect#r moved by \\(5, 7\\)")

add_test(NAME cli_history_limit COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; move r 5 7; history 1")
set_tests_properties(cli_history_limit PROPERTIES
    PASS_REGULAR_EXPRESSION "3 +rect#r moved by \\(5, 7\\)"
    FAIL_REGULAR_EXPRESSION "2 +rect#r created")

add_test(NAME cli_history_after_canvas COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; canvas 800 600; history")
set_tests_properties(cli_history_after_canvas PROPERTIES PASS_REGULAR_EXPRESSION "no history")

add_test(NAME cli_checkout COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; move r 5 7; checkout 2; show r; checkout 3; show r")
set_tests_properties(cli_checkout PROPERTIES PASS_REGULAR_EXPRESSION
    "checked out 2: rect#r created\nrect#r  parent=svg\n  x=0 y=0 w=1 h=1\nchecked out 3: rect#r moved by \\(5, 7\\)\nrect#r  parent=svg\n  x=5 y=7 w=1 h=1")

add_test(NAME cli_checkout_unknown COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; checkout 999")
set_tests_properties(cli_checkout_unknown PROPERTIES PASS_REGULAR_EXPRESSION
    "error: no such commit: 999")

# A parameter with a space is one word when it is quoted, and the answer prints it back
# the same way so the reader can copy it into another instruction.
add_test(NAME cli_invalid_quoted_id COMMAND $<TARGET_FILE:Cli> "init; create g \"a b\"")
set_tests_properties(cli_invalid_quoted_id PROPERTIES PASS_REGULAR_EXPRESSION
    "error: invalid XML id: \"a b\"")

add_test(NAME cli_quoted_id_error COMMAND $<TARGET_FILE:Cli> "init; delete \"id with space\"")
set_tests_properties(cli_quoted_id_error PROPERTIES PASS_REGULAR_EXPRESSION "error: no such shape: \"id with space\"")

add_test(NAME cli_quoted_attribute_error COMMAND $<TARGET_FILE:Cli> "init; create rect r 0 0 1 1; set r \"bad attr\" 1")
set_tests_properties(cli_quoted_attribute_error PROPERTIES PASS_REGULAR_EXPRESSION
    "error: unknown attribute: \"bad attr\"")

add_test(NAME cli_show_class COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 0 0 1 1; set r class \"a b\"; show r")
set_tests_properties(cli_show_class PROPERTIES PASS_REGULAR_EXPRESSION "class=\"a b\"")

# `tree` and `show` list the fields in the order `save` writes them.
add_test(NAME cli_tree_attribute_order COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 1 2 3 4 red; set r class c; set r stroke blue; set r stroke-width 2; set r transform \"translate(5 6)\"; tree")
set_tests_properties(cli_tree_attribute_order PROPERTIES PASS_REGULAR_EXPRESSION
    "class=c x=1 y=2 w=3 h=4 fill=red stroke=blue stroke-width=2 transform=translate\\(5 6\\)")

add_test(NAME cli_show_attribute_order COMMAND $<TARGET_FILE:Cli>
    "init; create rect r 1 2 3 4 red; set r class c; set r stroke blue; set r stroke-width 2; set r transform \"translate(5 6)\"; show r")
set_tests_properties(cli_show_attribute_order PROPERTIES PASS_REGULAR_EXPRESSION
    "class=c\n  x=1 y=2 w=3 h=4\n  fill=red\n  stroke=blue\n  stroke-width=2\n  transform=translate\\(5 6\\)")

add_test(NAME cli_quoted_path COMMAND $<TARGET_FILE:Cli> "read \"no such file.svg\"")
set_tests_properties(cli_quoted_path PROPERTIES PASS_REGULAR_EXPRESSION "error: cannot read file: \"no such file.svg\"")

# A viewBox and the attributes of a text are editable and survive a save/read round trip.
add_test(NAME cli_set_view_box COMMAND $<TARGET_FILE:Cli>
    "init; set svg viewBox \"0 0 1024 768\"; show svg")
set_tests_properties(cli_set_view_box PROPERTIES PASS_REGULAR_EXPRESSION "viewBox=\"0 0 1024 768\"")

add_test(NAME cli_read_unsized_view_box COMMAND $<TARGET_FILE:Cli>
    "read ${CMAKE_SOURCE_DIR}/tests/fixtures/viewbox-only.svg; show svg")
set_tests_properties(cli_read_unsized_view_box PROPERTIES PASS_REGULAR_EXPRESSION
    "svg \\(unsized\\) viewBox=\"0 0 100 50\"")

add_test(NAME cli_read_partial_canvas COMMAND $<TARGET_FILE:Cli>
    "read ${CMAKE_SOURCE_DIR}/tests/fixtures/width-only.svg; show svg")
set_tests_properties(cli_read_partial_canvas PROPERTIES PASS_REGULAR_EXPRESSION
    "svg \\(300 x unset\\)")

add_test(NAME cli_invalid_view_box COMMAND $<TARGET_FILE:Cli>
    "init; set svg viewBox \"0 0 -1 10\"")
set_tests_properties(cli_invalid_view_box PROPERTIES PASS_REGULAR_EXPRESSION "cannot read .* as a viewBox")

add_test(NAME cli_set_text_style COMMAND $<TARGET_FILE:Cli>
    "init; create text t 20 40 \"hello\"; set t text-anchor middle; set t font-size 12pt; show t")
set_tests_properties(cli_set_text_style PROPERTIES PASS_REGULAR_EXPRESSION
    "text-anchor=middle font-size=12pt")

add_test(NAME cli_save_svg_attributes COMMAND $<TARGET_FILE:Cli>
    "init; set svg xmlns \"urn:example:svg\"; set svg viewBox \"0 0 100 50\"; create text t 1 2 \"hi\"; set t text-anchor end; set t font-size 8px; save ${CMAKE_CURRENT_BINARY_DIR}/cli_svg_attributes.svg; read ${CMAKE_CURRENT_BINARY_DIR}/cli_svg_attributes.svg; show svg")
set_tests_properties(cli_save_svg_attributes PROPERTIES PASS_REGULAR_EXPRESSION
    "svg xmlns=\"urn:example:svg\" \\(800x600\\) viewBox=\"0 0 100 50\"")

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
