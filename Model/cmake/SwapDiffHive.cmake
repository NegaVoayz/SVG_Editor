# SwapDiffHive is Model's own dependency: the hive is the arena the SVG tree is
# stored in, and it is Model's business alone. Keeping the fetch here is part of
# how that rule holds - nothing but Model/CMakeLists.txt includes this file.
#
# The link itself is PUBLIC, because Document holds the arena by value and so its
# header reaches the hive through TreeStore.hh. Being able to see the hive is not
# being allowed to use it: `cmake/CheckLayering.cmake` fails the build of the tests
# if any module outside Model/ names it.
include(FetchContent)

set(SVG_EDITOR_SWAPDIFFHIVE_REPOSITORY "https://github.com/NegaVoayz/SwapDiffHive.git"
    CACHE STRING "Repository the SwapDiffHive dependency is fetched from")
set(SVG_EDITOR_SWAPDIFFHIVE_TAG "v1.1.0"
    CACHE STRING "Tag or commit of SwapDiffHive to check out")

# FIND_PACKAGE_ARGS lets an already installed SwapDiffHive win, which is what
# makes an offline machine work without a source checkout; otherwise the git
# repository above is cloned into the build tree. A local checkout can also be
# used as it is by pointing FETCHCONTENT_SOURCE_DIR_SWAPDIFFHIVE at it, which is
# what the local-dep preset does. GIT_SHALLOW is left off so that the tag above
# can be swapped for a commit.
FetchContent_Declare(SwapDiffHive
    GIT_REPOSITORY "${SVG_EDITOR_SWAPDIFFHIVE_REPOSITORY}"
    GIT_TAG "${SVG_EDITOR_SWAPDIFFHIVE_TAG}"
    FIND_PACKAGE_ARGS 1.1 CONFIG)

FetchContent_MakeAvailable(SwapDiffHive)
