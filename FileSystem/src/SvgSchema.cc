#include "SvgSchema.hh"

#include <cstddef>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "ElementName.hh"
#include "SvgFields.hh"

namespace svg_schema {
namespace {

using AttributeMatcher = bool (*)(std::string_view);

struct SchemaRow {
    std::string_view element;
    ElementSchema schema;
    AttributeMatcher accepts_specific;
};

template<typename Fields>
bool accepts(const Fields& fields, std::string_view attribute) {
    bool result = false;
    std::apply(
        [&](const auto&... field) {
            const auto matches = [&](const auto& descriptor) {
                if (descriptor.name == attribute) return true;
                for (const std::string_view alias : descriptor.aliases) {
                    if (alias == attribute) return true;
                }
                return false;
            };
            result = (result || ... || matches(field));
        },
        fields);
    return result;
}

bool accepts_nothing(std::string_view) { return false; }

inline constexpr SchemaRow rows[] = {
    {"svg", {ElementFamily::Container}, [](std::string_view attr) {
         return accepts(svg_fields::svg_fields, attr);
     }},
    {"g", {ElementFamily::Container}, [](std::string_view attr) {
         return accepts(svg_fields::group_fields, attr);
     }},
    {"defs", {ElementFamily::Container}, accepts_nothing},
    {"symbol", {ElementFamily::Container}, accepts_nothing},
    {"switch", {ElementFamily::Container}, accepts_nothing},
    {"rect", {ElementFamily::Shape}, [](std::string_view attr) {
         return accepts(svg_fields::rect_fields, attr);
     }},
    {"circle", {ElementFamily::Shape}, [](std::string_view attr) {
         return accepts(svg_fields::circle_fields, attr);
     }},
    {"ellipse", {ElementFamily::Shape}, [](std::string_view attr) {
         return accepts(svg_fields::ellipse_fields, attr);
     }},
    {"line", {ElementFamily::Shape}, [](std::string_view attr) {
         return accepts(svg_fields::line_fields, attr);
     }},
    {"text", {ElementFamily::Text}, [](std::string_view attr) {
         return accepts(svg_fields::text_common_fields, attr);
     }},
    {"tspan", {ElementFamily::Text}, [](std::string_view attr) {
         return accepts(svg_fields::text_common_fields, attr);
     }},
    {"textPath", {ElementFamily::Text}, [](std::string_view attr) {
         return accepts(svg_fields::text_path_attributes, attr);
     }},
};

constexpr std::optional<SchemaRow> find_row(std::string_view element) {
    for (const SchemaRow& row : rows) {
        if (row.element == element) return row;
    }
    return std::nullopt;
}

template<typename Element>
consteval bool has_schema() {
    return find_row(element_name(Element{})).has_value();
}

template<typename Element>
consteval bool has_family(ElementFamily family) {
    const std::optional<SchemaRow> row = find_row(element_name(Element{}));
    return row.has_value() && row->schema.family == family;
}

template<typename Variant>
consteval bool has_schema_for_every_alternative() {
    return []<std::size_t... Index>(std::index_sequence<Index...>) {
        return (has_schema<std::variant_alternative_t<Index, Variant>>() && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template<typename Variant>
consteval bool has_family_for_every_alternative(ElementFamily family) {
    return [family]<std::size_t... Index>(std::index_sequence<Index...>) {
        return (has_family<std::variant_alternative_t<Index, Variant>>(family) && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

inline constexpr std::size_t payload_element_count =
    std::variant_size_v<Shape::Kinds> + std::variant_size_v<Container::Kinds> + std::variant_size_v<Text::Kinds>;
static_assert(std::size(rows) == payload_element_count,
              "the SVG schema must have one row per element payload alternative");
static_assert(has_schema_for_every_alternative<Shape::Kinds>(),
              "the SVG schema is missing a shape alternative");
static_assert(has_schema_for_every_alternative<Container::Kinds>(),
              "the SVG schema is missing a container alternative");
static_assert(has_schema_for_every_alternative<Text::Kinds>(),
              "the SVG schema is missing a text alternative");
static_assert(has_family_for_every_alternative<Shape::Kinds>(ElementFamily::Shape),
              "a shape alternative has the wrong schema family");
static_assert(has_family_for_every_alternative<Container::Kinds>(ElementFamily::Container),
              "a container alternative has the wrong schema family");
static_assert(has_family_for_every_alternative<Text::Kinds>(ElementFamily::Text),
              "a text alternative has the wrong schema family");

}  // namespace

std::optional<ElementSchema> schema_for(std::string_view element) {
    const std::optional<SchemaRow> row = find_row(element);
    if (!row) return std::nullopt;
    return row->schema;
}

bool is_element(std::string_view element) { return find_row(element).has_value(); }

bool accepts_attribute(std::string_view element, std::string_view attribute) {
    const std::optional<SchemaRow> row = find_row(element);
    if (!row) return false;
    return accepts(svg_fields::common_fields, attribute) || row->accepts_specific(attribute);
}

}  // namespace svg_schema
