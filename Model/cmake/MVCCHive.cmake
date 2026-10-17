# MVCCHive is Model's own dependency: the hive is the arena the SVG tree is
# stored in, and it is Model's business alone. Keeping the fetch here is part of
# how that rule holds - nothing but Model/CMakeLists.txt includes this file.
#
# The link is PUBLIC while TreeStore.hh names the hive in its member layout, and
# the layering test is what keeps every module outside Model/ from using it.
include(FetchContent)

set(SVG_EDITOR_MVCCHIVE_REPOSITORY "https://github.com/NegaVoayz/MVCCHive.git"
    CACHE STRING "Repository the MVCCHive dependency is fetched from")
set(SVG_EDITOR_MVCCHIVE_TAG "v1.0.0"
    CACHE STRING "Tag or commit of MVCCHive to check out")

# An installed MVCCHive wins, which is what makes an offline machine work
# without a source checkout. The local-dep preset points the source directory at
# the sibling checkout instead.
FetchContent_Declare(MVCCHive
    GIT_REPOSITORY "${SVG_EDITOR_MVCCHIVE_REPOSITORY}"
    GIT_TAG "${SVG_EDITOR_MVCCHIVE_TAG}"
    FIND_PACKAGE_ARGS 1.0 CONFIG)

# The hive has its own test suite. Model tests the document through Model's API,
# so the dependency's tests are not part of this project's test run.
set(MVCC_HIVE_BUILD_TESTS OFF CACHE BOOL "Build MVCCHive tests" FORCE)
FetchContent_MakeAvailable(MVCCHive)
