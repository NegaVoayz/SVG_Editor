# FileSystem is the context I/O module. It depends on Model for the document values,
# Protocol for the typed context intents and outcomes, and tinyxml2 for XML syntax.
include(cmake/TinyXml2.cmake)

add_library(FileSystem
    src/FileSystem.cc
    src/SvgDocument.cc
    src/SvgReader.cc
    src/SvgSchema.cc
    src/SvgValueReader.cc
    src/SvgWriter.cc
    src/TinyXml2Adapter.cc
    src/XmlSnapshot.cc)
add_library(SvgEditor::FileSystem ALIAS FileSystem)

target_include_directories(FileSystem PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

target_compile_features(FileSystem PUBLIC cxx_std_23)
target_link_libraries(FileSystem
    PUBLIC
        SvgEditor::Model
        SvgEditor::Protocol
    PRIVATE
        tinyxml2::tinyxml2
        SvgEditor::Warnings)

if(BUILD_TESTING)
    add_executable(file_system_test tests/FileSystem_test.cc)
    target_include_directories(file_system_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(file_system_test PRIVATE FileSystem SvgEditor::Warnings)
    add_test(NAME file_system COMMAND file_system_test)

    add_executable(svg_io_test tests/svg_io_test.cc)
    target_include_directories(svg_io_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(svg_io_test PRIVATE FileSystem SvgEditor::Warnings)
    add_test(NAME svg_io COMMAND svg_io_test)
endif()
