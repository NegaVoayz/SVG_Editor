#include "LoadDocument.hh"

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {

inline constexpr std::size_t nodes_per_commit = 1024;

struct Pending {
    std::size_t index;
    Id parent;
};

}  // namespace

void load_parsed(TreeStore& store, const ParsedSvg& parsed) {
    if (parsed.nodes.empty() || parsed.root >= parsed.nodes.size()) {
        throw std::invalid_argument("load_parsed: the parsed document has no root");
    }

    store.reset(std::nullopt, std::nullopt);

    std::vector<Pending> pending{{parsed.root, no_id}};
    std::size_t loaded = 0;

    while (loaded < parsed.nodes.size()) {
        if (pending.empty()) throw std::invalid_argument("load_parsed: the parsed tree is disconnected");
        auto edit = store.begin();
        std::size_t batch = 0;

        while (!pending.empty() && batch < nodes_per_commit) {
            const Pending item = pending.back();
            pending.pop_back();

            Node payload = parsed.nodes[item.index].payload;
            const Id transform = store.install_transforms(edit, parsed.nodes[item.index].transforms);
            if (transform != no_id) {
                if (!std::holds_alternative<SVGNode>(payload)) {
                    throw std::invalid_argument("load_parsed: character data carries a transform");
                }
                std::get<SVGNode>(payload).transform_attr.head = transform;
            }
            Id handle = no_id;
            if (item.index == parsed.root) {
                handle = store.root();
                store.update(edit, handle, payload);
            } else {
                handle = store.append_child(edit, item.parent, payload);
            }
            ++loaded;
            ++batch;

            const std::vector<std::size_t>& children = parsed.nodes[item.index].children;
            for (std::size_t index = children.size(); index > 0; --index) {
                pending.push_back(Pending{children[index - 1], handle});
            }
        }

        store.commit(edit);
    }

    store.clear_history();
}
