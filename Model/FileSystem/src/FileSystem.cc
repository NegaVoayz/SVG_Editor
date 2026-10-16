#include "FileSystem.hh"

#include <fstream>
#include <sstream>

namespace FileSystem {

FileResult read(std::string_view path) {
    std::ifstream file{std::string(path), std::ios::binary};
    if (!file) return FileResult{false, "cannot open " + std::string(path)};

    std::ostringstream bytes;
    bytes << file.rdbuf();
    if (file.bad()) return FileResult{false, "cannot read " + std::string(path)};
    return FileResult{true, bytes.str()};
}

FileResult write(std::string_view path, std::string_view text) {
    std::ofstream file{std::string(path), std::ios::binary | std::ios::trunc};
    if (!file) return FileResult{false, "cannot open " + std::string(path)};

    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) return FileResult{false, "cannot write " + std::string(path)};
    return FileResult{true, {}};
}

}  // namespace FileSystem
