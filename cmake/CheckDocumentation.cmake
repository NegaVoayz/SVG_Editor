set(docs_dir "${SOURCE_DIR}/docs")
set(required
    README.md
    Structure.md
    Pipeline.md
    Model.md
    Process.md
    QualityAssurance.md
    Consistency.md)

foreach(document IN LISTS required)
    if(NOT EXISTS "${docs_dir}/${document}")
        message(FATAL_ERROR "documentation: required document is missing: docs/${document}")
    endif()
endforeach()

# Only registered submission chapters are checked. Untracked working notes are local
# evidence, and a stale path in one must not turn the deliverable's self-check red.
foreach(document IN LISTS required)
    file(READ "${docs_dir}/${document}" text)
    # A C++ snippet can contain `](...)` as a lambda call. Markdown links and repository
    # references are claims in prose, so remove fenced code before scanning them.
    string(REGEX REPLACE "```[^`]*```" "" prose "${text}")

    # Relative Markdown links must resolve. The only fragment currently used is one
    # GitHub-style heading fragment, so validate that its heading slug exists.
    set(link_cursor 0)
    while(1)
        string(SUBSTRING "${prose}" "${link_cursor}" -1 remaining_text)
        string(FIND "${remaining_text}" "](" relative_open)
        if(relative_open EQUAL -1)
            break()
        endif()
        math(EXPR link_open "${link_cursor} + ${relative_open}")
        math(EXPR target_start "${link_open} + 2")
        string(SUBSTRING "${prose}" "${target_start}" -1 after_open)
        string(FIND "${after_open}" ")" relative_close)
        if(relative_close EQUAL -1)
            break()
        endif()
        math(EXPR link_close "${target_start} + ${relative_close}")
        math(EXPR target_length "${link_close} - ${target_start}")
        string(SUBSTRING "${prose}" "${target_start}" "${target_length}" target)
        math(EXPR link_cursor "${link_close} + 1")

        if(target MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:")
            continue()
        endif()

        set(path "${target}")
        set(fragment "")
        if(target MATCHES "^([^#]+)#(.+)$")
            set(path "${CMAKE_MATCH_1}")
            set(fragment "${CMAKE_MATCH_2}")
        endif()
        set(resolved "${docs_dir}/${path}")
        if(NOT EXISTS "${resolved}")
            message(FATAL_ERROR
                "documentation: ${document} links to missing file '${target}'")
        endif()

        if(NOT fragment STREQUAL "")
            file(READ "${resolved}" linked_text)
            string(REGEX MATCHALL "#+ [^\n]+" headings "${linked_text}")
            set(found FALSE)
            foreach(heading IN LISTS headings)
                string(REGEX REPLACE "^#+ " "" heading_text "${heading}")
                string(TOLOWER "${heading_text}" slug)
                string(REPLACE " " "-" slug "${slug}")
                string(REGEX REPLACE "[^a-z0-9_-]" "" slug "${slug}")
                if(slug STREQUAL fragment)
                    set(found TRUE)
                    break()
                endif()
            endforeach()
            if(NOT found)
                message(FATAL_ERROR
                    "documentation: ${document} links to missing heading '${target}'")
            endif()
        endif()
    endwhile()

    # Source paths named in backticks are executable claims, not examples. Check both a
    # plain repository path and the optional :line suffix.
    string(REGEX MATCHALL "`[^`]+`" code_words "${prose}")
    foreach(raw IN LISTS code_words)
        string(REGEX REPLACE "^`([^`]+)`$" "\\1" word "${raw}")
        set(path "${word}")
        set(line "")
        if(word MATCHES "^(.+\\.(cc|hh|md|svg|cmake)):([0-9]+)$")
            set(path "${CMAKE_MATCH_1}")
            set(line "${CMAKE_MATCH_3}")
        elseif(NOT word MATCHES "^.+/[^/]+\\.(cc|hh|md|svg|cmake)$")
            continue()
        endif()

        set(resolved "${SOURCE_DIR}/${path}")
        if(NOT EXISTS "${resolved}")
            message(FATAL_ERROR "documentation: missing source path '${word}'")
        endif()
        if(NOT line STREQUAL "")
            file(STRINGS "${resolved}" source_lines)
            list(LENGTH source_lines source_line_count)
            if(line LESS 1 OR line GREATER source_line_count)
                message(FATAL_ERROR
                    "documentation: line ${line} is outside '${path}' (${source_line_count} lines)")
            endif()
        endif()
    endforeach()

    # Any hexadecimal revision is a repository reference. Keep the pattern to seven or
    # more characters so CSS colours and ordinary words are not treated as revisions.
    string(REGEX MATCHALL "[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]+"
        revisions "${prose}")
    foreach(revision IN LISTS revisions)
        execute_process(
            COMMAND git -C "${SOURCE_DIR}" cat-file -e "${revision}^{commit}"
            RESULT_VARIABLE revision_status
            OUTPUT_QUIET
            ERROR_QUIET)
        if(NOT revision_status EQUAL 0)
            message(FATAL_ERROR
                "documentation: ${document} names unknown commit '${revision}'")
        endif()
    endforeach()
endforeach()

file(READ "${docs_dir}/Consistency.md" consistency)
function(recorded_metric label variable)
    if(NOT consistency MATCHES "\\| ${label} \\| ([0-9]+) \\|")
        message(FATAL_ERROR "documentation: Consistency.md does not record '${label}'")
    endif()
    set(${variable} "${CMAKE_MATCH_1}" PARENT_SCOPE)
endfunction()

recorded_metric("Source commits at the validated snapshot" recorded_commits)
recorded_metric("CTest registrations" recorded_tests)
recorded_metric("static_assert occurrences" recorded_assertions)
recorded_metric("source files containing static_assert" recorded_assertion_files)

execute_process(
    COMMAND "${CTEST_COMMAND}" --test-dir "${BINARY_DIR}" --show-only=json-v1
    OUTPUT_VARIABLE test_json
    ERROR_VARIABLE test_error
    RESULT_VARIABLE ctest_status)
if(NOT ctest_status EQUAL 0)
    message(FATAL_ERROR "documentation: cannot inspect CTest registrations: ${test_error}")
endif()
string(JSON measured_tests LENGTH "${test_json}" tests)
if(NOT measured_tests STREQUAL recorded_tests)
    message(FATAL_ERROR
        "documentation: CTest has ${measured_tests} tests, recorded as ${recorded_tests}")
endif()

file(GLOB_RECURSE source_files
    "${SOURCE_DIR}/*.cc"
    "${SOURCE_DIR}/*.hh")
set(measured_assertions 0)
set(measured_assertion_files 0)
foreach(source IN LISTS source_files)
    if(source MATCHES "/build/")
        continue()
    endif()
    file(STRINGS "${source}" lines)
    set(file_has_assertion FALSE)
    foreach(line IN LISTS lines)
        if(line MATCHES "static_assert")
            math(EXPR measured_assertions "${measured_assertions} + 1")
            set(file_has_assertion TRUE)
        endif()
    endforeach()
    if(file_has_assertion)
        math(EXPR measured_assertion_files "${measured_assertion_files} + 1")
    endif()
endforeach()
if(NOT measured_assertions STREQUAL recorded_assertions)
    message(FATAL_ERROR
        "documentation: found ${measured_assertions} static_assert lines, recorded as ${recorded_assertions}")
endif()
if(NOT measured_assertion_files STREQUAL recorded_assertion_files)
    message(FATAL_ERROR
        "documentation: found ${measured_assertion_files} assertion-bearing files, recorded as ${recorded_assertion_files}")
endif()

function(expect_cli label command expected)
    execute_process(
        COMMAND "${CLI}" "${command}"
        OUTPUT_VARIABLE output
        ERROR_VARIABLE error
        RESULT_VARIABLE cli_status)
    if(NOT cli_status MATCHES "^-?[0-9]+$" OR
       (NOT cli_status EQUAL 0 AND output STREQUAL "" AND error STREQUAL ""))
        message(FATAL_ERROR
            "documentation: cannot run CLI transcript '${label}' "
            "(status ${cli_status}): ${error}")
    endif()
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR
            "documentation: CLI transcript '${label}' does not match.\n"
            "command: ${command}\n"
            "expected substring:\n${expected}\n"
            "actual output:\n${output}\n${error}")
    endif()
endfunction()

expect_cli("polygon"
    "init; create polygon tri \"0,0 10,0 5,10\" #336699; show tri"
    "polygon#tri  parent=svg\n  points=\"0,0 10,0 5,10\"\n  fill=#336699")
expect_cli("box marker"
    "init; create box status 0 0 10 10 \"L\"; show status"
    "g#status (box)  parent=svg")
expect_cli("zoom"
    "init; create rect r 0 0 1 1; zoom 2; history"
    "zoom 2x")
expect_cli("missing box label"
    "read ${SOURCE_DIR}/tests/fixtures/box-without-label.svg; set b content \"x\""
    "error: composite g#b has no label child")

execute_process(
    COMMAND git -C "${SOURCE_DIR}" rev-list --count HEAD
    OUTPUT_VARIABLE measured_commits
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_status)
if(NOT git_status EQUAL 0 OR NOT measured_commits STREQUAL recorded_commits)
    message(FATAL_ERROR
        "documentation: commit count is ${measured_commits}, recorded as ${recorded_commits}")
endif()

message(STATUS
    "documentation: ${measured_commits} commits, ${measured_tests} tests, "
    "${measured_assertions} static_assert lines in ${measured_assertion_files} files")
