# The shared vocabulary of the pipeline: the instruction a front end parses a line
# into, the outcome the Planner hands back, and the diagnostics FileSystem reports.
# Header only, and it is where the
# project's language level travels to the modules above Model - C++23, which is the
# language this project is written in. Model states its own, and the dependency
# states its own for the code that uses it.
add_library(Protocol INTERFACE)
add_library(SvgEditor::Protocol ALIAS Protocol)

target_include_directories(Protocol INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# The vocabulary is made of model values - a length, a colour, a handle - so the
# protocol depends on Model for its types and on nothing else.
target_compile_features(Protocol INTERFACE cxx_std_23)
target_link_libraries(Protocol
    INTERFACE
        SvgEditor::Model
        SvgEditor::Warnings)
