# Model owns the tree: the arena it is stored in, the API that reads it and the
# API that edits it. MVCCHive is its dependency, so this directory is the only
# place in the build where the hive is used.
include(cmake/MVCCHive.cmake)

add_library(Model
    src/Color.cc
    src/Document.cc
    src/History.cc
    src/Units.cc
    src/TreeStore.cc)
add_library(SvgEditor::Model ALIAS Model)

target_compile_features(Model PUBLIC cxx_std_23)

target_include_directories(Model PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# PUBLIC, not PRIVATE: Document holds the arena by value, so its header includes
# TreeStore.hh and with it the hive. What must not travel is the *use* of the hive -
# no other module may open a hive transaction or touch MVCCHive, which the layering
# test checks.
target_link_libraries(Model
    PUBLIC
        mvcc_hive
    PRIVATE
        SvgEditor::Warnings)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
