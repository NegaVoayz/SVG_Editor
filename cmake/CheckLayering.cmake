# The one rule the target graph cannot express on its own: SwapDiffHive and its
# transactions belong to Model/. Document holds the arena by value, so the hive's
# headers are on every module's include path and the link line no longer says who
# may use it - so this reads the sources instead.
#
# Run by ctest as: cmake -DSOURCE_DIR=<project> -P cmake/CheckLayering.cmake

set(modules Editor Planner Ui Protocol tests)
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
            if(line MATCHES "SwapDiffHive")
                string(APPEND offenders "  ${source}: ${line}\n")
            endif()
        endforeach()
    endforeach()
endforeach()

if(offenders)
    message(FATAL_ERROR
        "SwapDiffHive belongs to Model/, and these lines reach for it:\n${offenders}"
        "An edit goes through Document::transaction(), a read through Document's view API.")
endif()

message(STATUS "layering: SwapDiffHive is used by Model/ alone")
