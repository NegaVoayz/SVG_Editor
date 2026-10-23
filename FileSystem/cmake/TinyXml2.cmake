# tinyxml2 parses XML syntax; the SVG reader above it never sees tinyxml2's
# pointer-based DOM. Keeping the fetch here also keeps the dependency private to
# Model, the same way MVCCHive belongs to Model alone.
include(FetchContent)

set(SVG_EDITOR_TINYXML2_REPOSITORY "https://github.com/leethomason/tinyxml2.git"
    CACHE STRING "Repository the tinyxml2 dependency is fetched from")
set(SVG_EDITOR_TINYXML2_TAG "11.0.0"
    CACHE STRING "Tag or commit of tinyxml2 to check out")

FetchContent_Declare(tinyxml2
    GIT_REPOSITORY "${SVG_EDITOR_TINYXML2_REPOSITORY}"
    GIT_TAG "${SVG_EDITOR_TINYXML2_TAG}"
    FIND_PACKAGE_ARGS 11 CONFIG)

set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "Build tinyxml2 tests" FORCE)
set(svg_editor_visibility_preset "${CMAKE_CXX_VISIBILITY_PRESET}")
set(svg_editor_visibility_inlines "${CMAKE_VISIBILITY_INLINES_HIDDEN}")
set(svg_editor_had_visibility_preset FALSE)
if(DEFINED CMAKE_CXX_VISIBILITY_PRESET AND NOT "${CMAKE_CXX_VISIBILITY_PRESET}" STREQUAL "")
    set(svg_editor_had_visibility_preset TRUE)
endif()
set(svg_editor_had_visibility_inlines FALSE)
if(DEFINED CMAKE_VISIBILITY_INLINES_HIDDEN AND NOT "${CMAKE_VISIBILITY_INLINES_HIDDEN}" STREQUAL "")
    set(svg_editor_had_visibility_inlines TRUE)
endif()
FetchContent_MakeAvailable(tinyxml2)
if(svg_editor_had_visibility_preset)
    set(CMAKE_CXX_VISIBILITY_PRESET "${svg_editor_visibility_preset}")
else()
    unset(CMAKE_CXX_VISIBILITY_PRESET)
endif()
if(svg_editor_had_visibility_inlines)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN "${svg_editor_visibility_inlines}")
else()
    unset(CMAKE_VISIBILITY_INLINES_HIDDEN)
endif()
