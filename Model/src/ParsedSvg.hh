#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "SvgDiagnostic.hh"
#include "SVGNode.hh"

// A staged SVG tree. It owns only values; the links that place nodes in the hive are
// installed after the whole file has parsed, so a failed read cannot touch Document.
struct ParsedNode {
    Node payload;
    std::string element;
    std::vector<std::size_t> children;
};

struct ParsedSvg {
    std::vector<ParsedNode> nodes;
    std::size_t root = 0;
    std::vector<AssignedId> assigned_ids;
};
