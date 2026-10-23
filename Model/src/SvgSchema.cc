#include "SvgSchema.hh"

#include <array>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <reflection-cpp/reflection.hpp>

#include "ElementName.hh"
#include "Exhaustive.hh"

namespace svg_schema {
namespace {

template<class = void>
struct AttributeField {};

// C++ identifiers stand in for SVG names. A trailing underscore makes a keyword usable
// (`class_`, `switch_`), a single underscore becomes a hyphen (`text_anchor`), and the
// namespace-prefix underscore becomes a colon (`xlink_href`). Camel-case names such as
// viewBox and startOffset are already legal identifiers and remain unchanged.
template<std::string_view const& Name>
struct SchemaName {
    static constexpr auto storage = [] {
        std::array<char, Name.size() + 1> characters{};
        std::size_t length = 0;
        for (std::size_t index = 0; index < Name.size(); ++index) {
            if (Name[index] != '_') {
                characters[length++] = Name[index];
            } else if (index + 1 == Name.size()) {
                continue;
            } else if (index == 5 && Name.starts_with("xlink_")) {
                characters[length++] = ':';
            } else {
                characters[length++] = '-';
            }
        }
        characters[length] = '\0';
        return characters;
    }();

    static constexpr std::size_t length = [] {
        std::size_t result = 0;
        while (storage[result] != '\0') ++result;
        return result;
    }();

    static constexpr std::string_view value{storage.data(), length};
};

template<typename Fields>
inline constexpr auto attribute_names = []<std::size_t... Index>(std::index_sequence<Index...>) {
    static_assert((std::same_as<Reflection::MemberTypeOf<Index, Fields>, AttributeField<>> && ...),
                  "every reflected attribute must be an AttributeField marker");
    return std::array<std::string_view, sizeof...(Index)>{
        SchemaName<Reflection::MemberNameOf<Index, Fields>>::value...,
    };
}(std::make_index_sequence<Reflection::CountMembers<Fields>>{});

struct CommonAttributes {
    AttributeField<> id;
    AttributeField<> class_;
    AttributeField<> fill;
    AttributeField<> stroke;
    AttributeField<> stroke_width;
    AttributeField<> transform;
};

struct SvgAttributes {
    AttributeField<> width;
    AttributeField<> height;
    AttributeField<> viewBox;
    AttributeField<> xmlns;
};

struct GroupAttributes {
    AttributeField<> data_kind;
};

struct RectAttributes {
    AttributeField<> x;
    AttributeField<> y;
    AttributeField<> width;
    AttributeField<> height;
};

struct CircleAttributes {
    AttributeField<> cx;
    AttributeField<> cy;
    AttributeField<> r;
};

struct EllipseAttributes {
    AttributeField<> cx;
    AttributeField<> cy;
    AttributeField<> rx;
    AttributeField<> ry;
};

struct LineAttributes {
    AttributeField<> x1;
    AttributeField<> y1;
    AttributeField<> x2;
    AttributeField<> y2;
};

struct TextAttributes {
    AttributeField<> x;
    AttributeField<> y;
    AttributeField<> dx;
    AttributeField<> dy;
    AttributeField<> text_anchor;
    AttributeField<> font_size;
};

struct TextPathAttributes {
    AttributeField<> x;
    AttributeField<> y;
    AttributeField<> dx;
    AttributeField<> dy;
    AttributeField<> text_anchor;
    AttributeField<> font_size;
    AttributeField<> startOffset;
    AttributeField<> href;
    AttributeField<> xlink_href;
};

inline constexpr auto common_attributes = attribute_names<CommonAttributes>;
inline constexpr auto svg_attributes = attribute_names<SvgAttributes>;
inline constexpr auto group_attributes = attribute_names<GroupAttributes>;
inline constexpr auto rect_attributes = attribute_names<RectAttributes>;
inline constexpr auto circle_attributes = attribute_names<CircleAttributes>;
inline constexpr auto ellipse_attributes = attribute_names<EllipseAttributes>;
inline constexpr auto line_attributes = attribute_names<LineAttributes>;
inline constexpr auto text_attributes = attribute_names<TextAttributes>;
inline constexpr auto text_path_attributes = attribute_names<TextPathAttributes>;
inline constexpr auto no_attributes = std::array<std::string_view, 0>{};

// The member names are the element names. `switch_` loses its trailing underscore through
// SchemaName, so the reflected aggregate and the SVG vocabulary have one spelling rule.
struct SvgSchemaTable {
    ElementSchema svg{ElementFamily::Container, svg_attributes};
    ElementSchema g{ElementFamily::Container, group_attributes};
    ElementSchema defs{ElementFamily::Container, no_attributes};
    ElementSchema symbol{ElementFamily::Container, no_attributes};
    ElementSchema switch_{ElementFamily::Container, no_attributes};
    ElementSchema rect{ElementFamily::Shape, rect_attributes};
    ElementSchema circle{ElementFamily::Shape, circle_attributes};
    ElementSchema ellipse{ElementFamily::Shape, ellipse_attributes};
    ElementSchema line{ElementFamily::Shape, line_attributes};
    ElementSchema text{ElementFamily::Text, text_attributes};
    ElementSchema tspan{ElementFamily::Text, text_attributes};
    ElementSchema textPath{ElementFamily::Text, text_path_attributes};
};

inline constexpr SvgSchemaTable elements{};

template<std::size_t... Index>
constexpr const ElementSchema* schema_for(std::string_view element, std::index_sequence<Index...>) {
    const ElementSchema* result = nullptr;
    ([&] {
        if (element == SchemaName<Reflection::MemberNameOf<Index, SvgSchemaTable>>::value) {
            result = &Reflection::GetMemberAt<Index>(elements);
        }
    }(),
     ...);
    return result;
}

constexpr bool is_element(std::string_view element) {
    return schema_for(element, std::make_index_sequence<Reflection::CountMembers<SvgSchemaTable>>{}) != nullptr;
}

template<typename Variant>
consteval bool has_schema_for_every_alternative() {
    return []<std::size_t... Index>(std::index_sequence<Index...>) {
        return (is_element(element_name(std::variant_alternative_t<Index, Variant>{})) && ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

template<typename Variant>
consteval bool has_family_for_every_alternative(ElementFamily family) {
    return [family]<std::size_t... Index>(std::index_sequence<Index...>) {
        return ((schema_for(element_name(std::variant_alternative_t<Index, Variant>{}),
                            std::make_index_sequence<Reflection::CountMembers<SvgSchemaTable>>{})
                     ->family == family) &&
                ...);
    }(std::make_index_sequence<std::variant_size_v<Variant>>{});
}

inline constexpr std::size_t payload_element_count =
    std::variant_size_v<Shape::Kinds> + std::variant_size_v<Container::Kinds> + std::variant_size_v<Text::Kinds>;
static_assert(Reflection::CountMembers<SvgSchemaTable> == payload_element_count,
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

const ElementSchema* schema_for(std::string_view element) {
    return schema_for(element, std::make_index_sequence<Reflection::CountMembers<SvgSchemaTable>>{});
}

bool is_element(std::string_view element) {
    return schema_for(element) != nullptr;
}

bool accepts_attribute(std::string_view element, std::string_view attribute) {
    const ElementSchema* schema = schema_for(element);
    if (schema == nullptr) return false;
    for (const std::string_view name : common_attributes) {
        if (name == attribute) return true;
    }
    for (const std::string_view name : schema->attributes) {
        if (name == attribute) return true;
    }
    return false;
}

}  // namespace svg_schema
