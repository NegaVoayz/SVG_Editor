# reflection-cpp supplies the static aggregate reflection used by SvgSchema.cc.
# It is a header-only dependency private to Model, just like the XML adapter.
include(FetchContent)

set(SVG_EDITOR_REFLECTION_CPP_REPOSITORY "https://github.com/contour-terminal/reflection-cpp.git"
    CACHE STRING "Repository the reflection-cpp dependency is fetched from")
set(SVG_EDITOR_REFLECTION_CPP_TAG "7c6dd273794cf6b8f6243b751d91360b7dc8cfa2"
    CACHE STRING "Tag or commit of reflection-cpp to check out")

FetchContent_Declare(reflection-cpp
    GIT_REPOSITORY "${SVG_EDITOR_REFLECTION_CPP_REPOSITORY}"
    GIT_TAG "${SVG_EDITOR_REFLECTION_CPP_TAG}"
    FIND_PACKAGE_ARGS 0.5 CONFIG)

set(REFLECTION_TESTING OFF CACHE BOOL "Build reflection-cpp tests" FORCE)
FetchContent_MakeAvailable(reflection-cpp)
