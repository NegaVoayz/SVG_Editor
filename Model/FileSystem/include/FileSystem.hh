#pragma once

#include <string>
#include <string_view>
#include <variant>

// The file layer under Model. It only moves bytes: reading a document and writing
// one, with no idea what the bytes mean. The Planner's context changes (open,
// save) arrive here through Document, so that no other module has to know where a
// document is stored.
struct FileBytes {
    std::string bytes;
};
struct FileWritten {};
struct FileReadError {
    std::string path;
};
struct FileWriteError {
    std::string path;
};
using FileResult = std::variant<FileBytes, FileWritten, FileReadError, FileWriteError>;

namespace FileSystem {

FileResult read(std::string_view path);
FileResult write(std::string_view path, std::string_view text);

}  // namespace FileSystem
