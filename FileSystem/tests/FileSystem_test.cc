#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "Document.hh"
#include "FileSystem.hh"
#include "SvgDocument.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

bool write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    SvgNode root = SvgNode::element("svg");
    root.add_attribute("width", "10\"");

    SvgNode text = SvgNode::element("text");
    text.add_attribute("id", "label");
    text.add_child(SvgNode::text("a & < b"));
    root.add_child(std::move(text));

    SvgNode group = SvgNode::element("g");
    group.add_child(SvgNode::element("rect"));
    root.add_child(std::move(group));

    const std::string expected = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                                 "<svg width=\"10&quot;\">\n"
                                 "  <text id=\"label\">a &amp; &lt; b</text>\n"
                                 "  <g>\n"
                                 "    <rect/>\n"
                                 "  </g>\n"
                                 "</svg>";
    CHECK(file_system_detail::svg_document(root) == expected);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "svg_editor_file_system";
    std::filesystem::create_directories(directory);
    const std::filesystem::path source = directory / "source.svg";
    const std::filesystem::path explicit_target = directory / "explicit.svg";
    CHECK(write_text(source, R"(<svg><g id="kept"/></svg>)"));

    Document document;
    FileSystem file_system{document};
    CHECK(file_system.translate(Init{}).ok());
    const Outcome no_source = file_system.translate(Save{std::nullopt});
    CHECK(!no_source.ok());
    CHECK(std::holds_alternative<fault::MissingSourcePath>(no_source.refusal()));

    const Outcome opened = file_system.translate(Read{source.string()});
    CHECK(opened.ok());
    CHECK(document.find("kept").has_value());
    CHECK(file_system.translate(Save{explicit_target.string()}).ok());
    CHECK(std::filesystem::exists(explicit_target));

    {
        Transaction edit = document.transaction();
        SVGNode element;
        element.identity_attr.id = "after-explicit-save";
        element.data = Container{ChildAttr{}, Container::Group{}};
        edit.append_child(document.root(), Node{std::move(element)});
        edit.commit();
    }
    CHECK(file_system.translate(Save{std::nullopt}).ok());
    {
        std::ifstream saved{source, std::ios::binary};
        const std::string bytes{std::istreambuf_iterator<char>{saved}, std::istreambuf_iterator<char>{}};
        CHECK(bytes.find("after-explicit-save") != std::string::npos);
    }

    const std::filesystem::path invalid = directory / "invalid.svg";
    CHECK(write_text(invalid, "<svg>"));
    CHECK(!file_system.translate(Read{invalid.string()}).ok());
    CHECK(document.find("after-explicit-save").has_value());
    CHECK(file_system.translate(Save{std::nullopt}).ok());

    CHECK(file_system.translate(Init{}).ok());
    CHECK(!file_system.translate(Save{std::nullopt}).ok());
    return failures == 0 ? 0 : 1;
}
