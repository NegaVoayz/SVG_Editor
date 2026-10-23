# 3.28 is what MVCCHive itself asks for; FIND_PACKAGE_ARGS in the fetch uses
# the same floor.
cmake_minimum_required(VERSION 3.28)

project(SvgEditor VERSION 0.1.0 LANGUAGES CXX)

# Testing is on by default. The tests fetch nothing, so they run offline once the
# dependency is in place.
include(CTest)

# Warnings are for our own code only: third party code that arrives as a
# dependency is compiled with whatever it was written for.
add_library(SvgEditorWarnings INTERFACE)
add_library(SvgEditor::Warnings ALIAS SvgEditorWarnings)
if(MSVC)
    target_compile_options(SvgEditorWarnings INTERFACE /W4 /permissive- /WX)
else()
    target_compile_options(SvgEditorWarnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

# The tree of targets, bottom up:
#
#   Cli -> Planner -> Editor -----> Model -> MVCCHive
#                 \-> FileSystem --> Model
#
# MVCCHive is Model's dependency: Model/ is the only directory that fetches it and
# the only one that uses it. FileSystem owns file syntax and I/O, while Editor owns
# tree edits; both reach the document only through Model's API.
add_subdirectory(Model)
add_subdirectory(Protocol)
add_subdirectory(Editor)
add_subdirectory(FileSystem)
add_subdirectory(Planner)
add_subdirectory(Ui)

# the integration test, from the same side the cli drives the pipeline
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
