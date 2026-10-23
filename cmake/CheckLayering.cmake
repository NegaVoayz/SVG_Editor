# The one rule the target graph cannot express on its own: MVCCHive and its
# transactions belong to Model/. Document holds the arena by value, so the hive's
# headers are on every module's include path and the link line no longer says who
# may use it - so this reads the sources instead.
#
# Run by ctest as: cmake -DSOURCE_DIR=<project> -P cmake/CheckLayering.cmake

set(modules Editor FileSystem Planner Ui Protocol tests)
set(offenders "")

foreach(module IN LISTS modules)
    file(GLOB_RECURSE sources
        "${SOURCE_DIR}/${module}/*.cc"
        "${SOURCE_DIR}/${module}/*.hh")
    if(NOT sources)
        # A module list that no longer matches the tree would quietly pass this test
        # for that module, so a name that finds nothing is a failure of its own.
        message(FATAL_ERROR "the layering check found no sources under ${SOURCE_DIR}/${module}")
    endif()
    foreach(source IN LISTS sources)
        file(STRINGS "${source}" lines)
        foreach(line IN LISTS lines)
            if(line MATCHES "^[ \t]*//")  # naming it in a comment is not using it
                continue()
            endif()
            if(line MATCHES "MVCCHive|mvcc_hive" OR
               (NOT module STREQUAL "FileSystem" AND line MATCHES "tinyxml2"))
                string(APPEND offenders "  ${source}: ${line}\n")
            endif()
        endforeach()
    endforeach()
endforeach()

if(offenders)
    message(FATAL_ERROR
        "MVCCHive belongs to Model and tinyxml2 belongs to FileSystem; these lines reach for them:\n${offenders}"
        "An edit goes through Editor, file I/O through FileSystem, and reads through Document.")
endif()

# Inside FileSystem, tinyxml2's pointer-based DOM is confined to the adapter. Every
# other source works with the pointer-free XmlSnapshot values instead.
file(GLOB_RECURSE filesystem_sources
    "${SOURCE_DIR}/FileSystem/*.cc"
    "${SOURCE_DIR}/FileSystem/*.hh")
foreach(source IN LISTS filesystem_sources)
    if(source MATCHES "TinyXml2Adapter\\.cc$")
        continue()
    endif()
    file(STRINGS "${source}" lines)
    foreach(line IN LISTS lines)
        if(line MATCHES "^[ \t]*//")
            continue()
        endif()
        if(line MATCHES "tinyxml2")
            message(FATAL_ERROR "tinyxml2 must stay inside FileSystem/src/TinyXml2Adapter.cc: ${source}: ${line}")
        endif()
    endforeach()
endforeach()

message(STATUS "layering: MVCCHive belongs to Model and tinyxml2 to FileSystem, behind one adapter")
