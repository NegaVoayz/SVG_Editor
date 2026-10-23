#pragma once

#include <string>
#include <string_view>
#include <variant>
#include <vector>

// The file layer under Model. It moves bytes and owns the XML line format: escaping,
// indentation, self-closing elements and inline character data. Model supplies a tree of
// already interpreted SvgNode values; FileSystem decides how those values become text.
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

struct SvgAttribute {
    std::string name;
    std::string value;
};

class SvgNode {
public:
    static SvgNode element(std::string name);
    static SvgNode text(std::string content);

    bool is_text() const;
    const std::string& name() const;
    const std::string& content() const;
    const std::vector<SvgAttribute>& attributes() const;
    const std::vector<SvgNode>& children() const;

    void add_attribute(std::string name, std::string value);
    void add_child(SvgNode child);

private:
    SvgNode();

    bool is_text_ = false;
    std::string name_;
    std::string content_;
    std::vector<SvgAttribute> attributes_;
    std::vector<SvgNode> children_;
};

namespace FileSystem {

FileResult read(std::string_view path);
FileResult write(std::string_view path, std::string_view text);
std::string svg_document(const SvgNode& root);

}  // namespace FileSystem
