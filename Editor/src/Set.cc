// `set <id> <attribute> <value>`: one named attribute of one node, and one transaction.
//
// The attribute is a type by the time it reaches here, so this file never looks a word up:
// `handle<Set>` visits the attribute variant, and each overload writes the member its type
// names. A node that does not carry that member answers with the attribute's own spelling,
// which is why a circle has no `x` and a rect has no `cx` without either being listed as a
// special case.
#include <cstddef>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "Naming.hh"
#include "NodeRef.hh"
#include "Outcome.hh"
#include "Translations.hh"

namespace {

std::optional<Refusal> finite_value(std::string_view attribute, LengthPercentage value) {
    if (!is_finite(value)) return fault::NonFiniteValue{std::string{attribute}};
    return std::nullopt;
}

std::optional<Refusal> nonnegative(std::string_view attribute, LengthPercentage value) {
    if (std::optional<Refusal> problem = finite_value(attribute, value)) return problem;
    if (numeric_value(value) < 0.0F) return fault::NegativeDimension{std::string{attribute}};
    return std::nullopt;
}

// Validate a dimension only when the node actually carries that member. A group asked
// for `w` should still be refused as an unknown attribute, not as a bad value.
template<typename HasMember>
std::optional<Refusal> dimension_if_present(const Document& document, Id node, std::string_view attribute,
                                            LengthPercentage value, HasMember has_member) {
    const std::optional<Node> payload = document.node(node);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Shape>(element.data)) return std::nullopt;

    const Shape& shape = std::get<Shape>(element.data);
    if (!std::visit([&has_member](const auto& specific) { return has_member(specific); }, shape.specific)) {
        return std::nullopt;
    }
    return nonnegative(attribute, value);
}

std::optional<Refusal> stroke_width_problem(const Document& document, Id node, LengthPercentage value) {
    const std::optional<Node> payload = document.node(node);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    return nonnegative(SetStrokeWidth::name, value);
}

std::optional<Refusal> view_box_problem(const Container::ViewBox& box) {
    if (!std::isfinite(box.min_x) || !std::isfinite(box.min_y) || !std::isfinite(box.width) ||
        !std::isfinite(box.height)) {
        return fault::NonFiniteValue{"viewBox"};
    }
    if (box.width < 0.0F || box.height < 0.0F) return fault::NegativeDimension{"viewBox"};
    return std::nullopt;
}

// `width` and `height` mean either a canvas side or a rect side. The canvas is the
// frame and must have a positive size; a rect may be zero and disappears, as SVG says.
std::optional<Refusal> canvas_or_rect_dimension(const Document& document, Id node, std::string_view attribute,
                                                LengthPercentage value) {
    if (!is_finite(value)) return fault::NonFiniteValue{std::string{attribute}};
    const std::optional<Node> payload = document.node(node);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(*payload);

    if (std::holds_alternative<Container>(element.data)) {
        const Container& container = std::get<Container>(element.data);
        if (std::holds_alternative<Container::SVG>(container.specific) && numeric_value(value) <= 0.0F) {
            return fault::NonPositiveCanvas{std::string{attribute}};
        }
    }
    if (std::holds_alternative<Shape>(element.data)) {
        const Shape& shape = std::get<Shape>(element.data);
        if (std::holds_alternative<Shape::Rect>(shape.specific)) {
            return nonnegative(attribute, value);
        }
    }
    return std::nullopt;
}

// One attribute of one SVG element: replace the payload, keep its place in the tree, and
// let the Model copy the old links into the replacement.
template<typename Attribute, typename Mutation>
Outcome set_element(Document& document, Id node, Mutation mutation) {
    std::optional<Node> payload = document.node(node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});
    if (!std::holds_alternative<SVGNode>(*payload) || !mutation(std::get<SVGNode>(*payload))) {
        return refused(Kind::Change, fault::UnknownAttribute{std::string{Attribute::name}});
    }

    Transaction edit = document.transaction();
    edit.update(node, std::move(*payload));
    edit.commit();
    return happened(Kind::Change, event::AttributeSet{name_of(document, node), std::string{Attribute::name}});
}

// A shape member exists on some alternatives of Shape and not on others. The visitor asks
// each alternative whether the member is there, so no kind is named and a shape added with
// a member of the same name answers the same attribute without changing this file.
template<typename Assign>
bool on_shape(SVGNode& element, Assign assign) {
    if (!std::holds_alternative<Shape>(element.data)) return false;
    return std::visit(assign, std::get<Shape>(element.data).specific);
}

template<typename Attribute, typename Assign>
Outcome set_shape(Document& document, Id node, Assign assign) {
    return set_element<Attribute>(document, node, [&assign](SVGNode& element) {
        return on_shape(element, assign);
    });
}

template<typename Member>
bool set_canvas_member(SVGNode& element, Member member, LengthPercentage value) {
    if (!std::holds_alternative<Container>(element.data)) return false;
    return std::visit(
        [&member, &value](auto& specific) -> bool {
            if constexpr (requires { specific.*member = std::optional<LengthPercentage>{value}; }) {
                specific.*member = value;
                return true;
            } else {
                return false;
            }
        },
        std::get<Container>(element.data).specific);
}

Outcome apply(Document& document, Id node, const SetX& intent) {
    if (std::optional<Refusal> problem = finite_value(SetX::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetX>(document, node, [value = intent.value](SVGNode& element) {
        if (std::holds_alternative<Text>(element.data)) {
            std::get<Text>(element.data).common.x = value;
            return true;
        }
        return on_shape(element, [value](auto& specific) -> bool {
            if constexpr (requires { specific.x = value; }) {
                specific.x = value;
                return true;
            } else {
                return false;
            }
        });
    });
}

Outcome apply(Document& document, Id node, const SetY& intent) {
    if (std::optional<Refusal> problem = finite_value(SetY::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetY>(document, node, [value = intent.value](SVGNode& element) {
        if (std::holds_alternative<Text>(element.data)) {
            std::get<Text>(element.data).common.y = value;
            return true;
        }
        return on_shape(element, [value](auto& specific) -> bool {
            if constexpr (requires { specific.y = value; }) {
                specific.y = value;
                return true;
            } else {
                return false;
            }
        });
    });
}

Outcome apply(Document& document, Id node, const SetW& intent) {
    if (std::optional<Refusal> problem = dimension_if_present(
            document, node, SetW::name, intent.value, [](const auto& specific) { return requires { specific.w; }; })) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetW>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.w = value; }) {
            specific.w = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetH& intent) {
    if (std::optional<Refusal> problem = dimension_if_present(
            document, node, SetH::name, intent.value, [](const auto& specific) { return requires { specific.h; }; })) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetH>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.h = value; }) {
            specific.h = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetCx& intent) {
    if (std::optional<Refusal> problem = finite_value(SetCx::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetCx>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.cx = value; }) {
            specific.cx = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetCy& intent) {
    if (std::optional<Refusal> problem = finite_value(SetCy::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetCy>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.cy = value; }) {
            specific.cy = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetR& intent) {
    if (std::optional<Refusal> problem = dimension_if_present(
            document, node, SetR::name, intent.value, [](const auto& specific) { return requires { specific.r; }; })) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetR>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.r = value; }) {
            specific.r = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetRx& intent) {
    if (std::optional<Refusal> problem = dimension_if_present(
            document, node, SetRx::name, intent.value, [](const auto& specific) { return requires { specific.rx; }; })) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetRx>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.rx = value; }) {
            specific.rx = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetRy& intent) {
    if (std::optional<Refusal> problem = dimension_if_present(
            document, node, SetRy::name, intent.value, [](const auto& specific) { return requires { specific.ry; }; })) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetRy>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.ry = value; }) {
            specific.ry = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetX1& intent) {
    if (std::optional<Refusal> problem = finite_value(SetX1::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetX1>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.x1 = value; }) {
            specific.x1 = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetY1& intent) {
    if (std::optional<Refusal> problem = finite_value(SetY1::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetY1>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.y1 = value; }) {
            specific.y1 = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetX2& intent) {
    if (std::optional<Refusal> problem = finite_value(SetX2::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetX2>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.x2 = value; }) {
            specific.x2 = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetY2& intent) {
    if (std::optional<Refusal> problem = finite_value(SetY2::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_shape<SetY2>(document, node, [value = intent.value](auto& specific) -> bool {
        if constexpr (requires { specific.y2 = value; }) {
            specific.y2 = value;
            return true;
        } else {
            return false;
        }
    });
}

Outcome apply(Document& document, Id node, const SetDx& intent) {
    if (std::optional<Refusal> problem = finite_value(SetDx::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetDx>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        std::get<Text>(element.data).common.dx = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetDy& intent) {
    if (std::optional<Refusal> problem = finite_value(SetDy::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetDy>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        std::get<Text>(element.data).common.dy = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetStrokeWidth& intent) {
    if (std::optional<Refusal> problem = stroke_width_problem(document, node, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetStrokeWidth>(document, node, [value = intent.value](SVGNode& element) {
        element.paint_attr.stroke_width = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetWidth& intent) {
    if (std::optional<Refusal> problem = canvas_or_rect_dimension(document, node, SetWidth::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetWidth>(document, node, [value = intent.value](SVGNode& element) {
        if (set_canvas_member(element, &Container::SVG::width, value)) return true;
        // `width` is the SVG name; `w` is the spelling `create` and the tree use. Both
        // reach the same side of a rect.
        return on_shape(element, [value](auto& specific) -> bool {
            if constexpr (requires { specific.w = value; }) {
                specific.w = value;
                return true;
            } else {
                return false;
            }
        });
    });
}

Outcome apply(Document& document, Id node, const SetHeight& intent) {
    if (std::optional<Refusal> problem = canvas_or_rect_dimension(document, node, SetHeight::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetHeight>(document, node, [value = intent.value](SVGNode& element) {
        if (set_canvas_member(element, &Container::SVG::height, value)) return true;
        return on_shape(element, [value](auto& specific) -> bool {
            if constexpr (requires { specific.h = value; }) {
                specific.h = value;
                return true;
            } else {
                return false;
            }
        });
    });
}

Outcome apply(Document& document, Id node, const SetStartOffset& intent) {
    if (std::optional<Refusal> problem = finite_value(SetStartOffset::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetStartOffset>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        Text& text = std::get<Text>(element.data);
        if (!std::holds_alternative<Text::Path>(text.specific)) return false;
        std::get<Text::Path>(text.specific).startOffset = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetXmlns& intent) {
    return set_element<SetXmlns>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Container>(element.data)) return false;
        Container& container = std::get<Container>(element.data);
        if (!std::holds_alternative<Container::SVG>(container.specific)) return false;
        std::get<Container::SVG>(container.specific).xmlns = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetViewBox& intent) {
    if (std::optional<Refusal> problem = view_box_problem(intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetViewBox>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Container>(element.data)) return false;
        Container& container = std::get<Container>(element.data);
        if (!std::holds_alternative<Container::SVG>(container.specific)) return false;
        std::get<Container::SVG>(container.specific).view_box = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetTextAnchor& intent) {
    return set_element<SetTextAnchor>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        std::get<Text>(element.data).common.text_anchor = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetFontSize& intent) {
    if (std::optional<Refusal> problem = nonnegative(SetFontSize::name, intent.value)) {
        return refused(Kind::Change, std::move(*problem));
    }
    return set_element<SetFontSize>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        std::get<Text>(element.data).common.font_size = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetClass& intent) {
    return set_element<SetClass>(document, node, [value = intent.value](SVGNode& element) {
        element.identity_attr.class_ = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetFill& intent) {
    return set_element<SetFill>(document, node, [value = intent.value](SVGNode& element) {
        element.paint_attr.fill = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetStroke& intent) {
    return set_element<SetStroke>(document, node, [value = intent.value](SVGNode& element) {
        element.paint_attr.stroke = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetTransform& intent) {
    if (intent.value && !is_finite(*intent.value)) {
        return refused(Kind::Change, fault::NonFiniteValue{"transform"});
    }
    return set_element<SetTransform>(document, node, [value = intent.value](SVGNode& element) {
        element.transform_attr.transform = value;
        return true;
    });
}

Outcome apply(Document& document, Id node, const SetHref& intent) {
    return set_element<SetHref>(document, node, [value = intent.value](SVGNode& element) {
        if (!std::holds_alternative<Text>(element.data)) return false;
        Text& text = std::get<Text>(element.data);
        if (!std::holds_alternative<Text::Path>(text.specific)) return false;
        std::get<Text::Path>(text.specific).href = value;
        return true;
    });
}

// A text's words are character data rather than an attribute of the element, so `text`
// writes the PlainText child. A text with none gets one; a text with several, as a parsed
// document may have, keeps its first and drops the rest so the words are said once. A
// PlainText handle names the character data directly and needs no walk.
Outcome set_text(Document& document, Id node, const std::string& value, std::string_view attribute) {
    std::optional<Node> payload = document.node(node);
    if (!payload) return refused(Kind::Change, fault::InternalError{});

    if (std::holds_alternative<PlainText>(*payload)) {
        PlainText text = std::get<PlainText>(*payload);
        text.content = value;
        Transaction edit = document.transaction();
        edit.update(node, Node{std::move(text)});
        edit.commit();
        return happened(Kind::Change, event::ContentSet{name_of(document, node)});
    }

    if (!std::holds_alternative<SVGNode>(*payload) ||
        !std::holds_alternative<Text>(std::get<SVGNode>(*payload).data)) {
        return refused(Kind::Change, fault::UnknownAttribute{std::string{attribute}});
    }

    std::vector<Id> text_children;
    for (const Id child : document.children(node)) {
        const std::optional<Node> child_payload = document.node(child);
        if (child_payload && std::holds_alternative<PlainText>(*child_payload)) text_children.push_back(child);
    }

    std::optional<Node> first;
    if (!text_children.empty()) first = document.node(text_children.front());

    Transaction edit = document.transaction();
    if (!first) {
        PlainText text;
        text.content = value;
        edit.append_child(node, Node{std::move(text)});
    } else {
        std::get<PlainText>(*first).content = value;
        edit.update(text_children.front(), std::move(*first));
        for (std::size_t index = 1; index < text_children.size(); ++index) edit.erase(text_children[index]);
    }
    edit.commit();
    return happened(Kind::Change, event::ContentSet{name_of(document, node)});
}

Outcome apply(Document& document, Id node, const SetText& intent) {
    return set_text(document, node, intent.value, SetText::name);
}

Outcome apply(Document& document, Id node, const SetContent& intent) {
    return set_text(document, node, intent.value, SetContent::name);
}

}  // namespace

template<> Outcome Editor::handle<Set>(const Set& intent) {
    const NodeRef& reference =
        std::visit([](const auto& attribute) -> const NodeRef& { return attribute.node; }, intent);
    const std::expected<Id, Refusal> node = resolve(reference);
    if (!node) return refused(Kind::Change, node.error());
    return std::visit([this, node = *node](const auto& attribute) { return apply(document_, node, attribute); }, intent);
}
