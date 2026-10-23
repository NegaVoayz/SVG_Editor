#include "LoadDocument.hh"

#include <utility>

void load_parsed(TreeStore& store, const ParsedSvg& parsed) {
    StagedTree staged;
    staged.root = parsed.root;
    staged.nodes.reserve(parsed.nodes.size());
    for (const ParsedNode& node : parsed.nodes) {
        staged.nodes.push_back(StagedNode{node.payload, node.children});
    }
    store.replace(std::move(staged));
}
