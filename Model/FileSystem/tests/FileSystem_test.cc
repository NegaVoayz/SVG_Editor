#include <cstdio>
#include <string>
#include <utility>

#include "FileSystem.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
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
    CHECK(FileSystem::svg_document(root) == expected);
    return failures == 0 ? 0 : 1;
}
