#pragma once

#include <span>
#include <string_view>

// The one reader/writer attribute contract. The reader refuses anything the schema does
// not name, and the writer validates every attribute against the same rule before it can
// emit it.
namespace svg_schema {

enum class ElementFamily {
    Shape,
    Container,
    Text,
};

struct ElementSchema {
    ElementFamily family;
    std::span<const std::string_view> attributes;
};

const ElementSchema* schema_for(std::string_view element);
bool is_element(std::string_view element);
bool accepts_attribute(std::string_view element, std::string_view attribute);

}  // namespace svg_schema
