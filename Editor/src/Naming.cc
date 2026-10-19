#include "Naming.hh"

#include <optional>
#include <string>
#include <variant>

#include "Document.hh"

event::NodeName name_of(const Document& document, Id node) {
    const std::optional<Node> payload = document.node(node);
    if (!payload) return event::NodeName{"unknown", {}};

    if (std::holds_alternative<SVGNode>(*payload)) {
        const SVGNode& element = std::get<SVGNode>(*payload);
        return event::NodeName{std::string{element_name(element)}, element.identity_attr.id};
    }

    // Character data has no element name or id of its own. The text element around it is
    // the thing the user sees and the thing an event should name.
    const std::optional<Id> parent = document.parent(node);
    if (parent) return name_of(document, *parent);
    return event::NodeName{"text", {}};
}
