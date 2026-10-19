// Transform storage: a node names the head of an immutable chain, an edit prepends one
// step, and undo only moves the node's head back to the chain it used to name.
#include <cstdio>
#include <optional>
#include <variant>
#include <vector>

#include "Document.hh"
#include "SVGNode.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

LengthPercentage user_units(float value) { return Length{value, Length::Unit::UserUnit}; }

Node group() {
    SVGNode element;
    element.identity_attr = IdentityAttr{"g", ""};
    element.data = Container{ChildAttr{}, Container::Group{}};
    return Node{std::move(element)};
}

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    Document document;
    document.reset(std::nullopt, std::nullopt);

    Id layer = no_id;
    {
        Transaction edit = document.transaction();
        layer = edit.append_child(document.root(), group());
        edit.commit();
    }

    CHECK(document.transforms(layer).empty());

    {
        Transaction edit = document.transaction();
        edit.prepend_transform(layer, Translate{user_units(2), user_units(3)});
        edit.commit();
    }
    {
        const std::vector<Transform> chain = document.transforms(layer);
        CHECK(chain.size() == 1);
        CHECK(std::holds_alternative<Translate>(chain[0]));
        if (std::holds_alternative<Translate>(chain[0])) {
            CHECK(to_text(std::get<Translate>(chain[0]).x) == "2");
            CHECK(to_text(std::get<Translate>(chain[0]).y) == "3");
        }
    }

    // A second move is a second step, not a sum: the chain remembers both edits.
    {
        Transaction edit = document.transaction();
        edit.prepend_transform(layer, Translate{user_units(1), user_units(1)});
        edit.commit();
    }
    {
        const std::vector<Transform> chain = document.transforms(layer);
        CHECK(chain.size() == 2);
        CHECK(std::holds_alternative<Translate>(chain[0]));
        CHECK(std::holds_alternative<Translate>(chain[1]));
        if (std::holds_alternative<Translate>(chain[0]) && std::holds_alternative<Translate>(chain[1])) {
            CHECK(to_text(std::get<Translate>(chain[0]).x) == "1");
            CHECK(to_text(std::get<Translate>(chain[1]).x) == "2");
        }
    }

    // A scale in front of the translations is the SVG order scale(...) translate(...).
    {
        Transaction edit = document.transaction();
        edit.prepend_transform(layer, Scale{2.0F, 2.0F});
        edit.commit();
    }
    {
        const std::vector<Transform> chain = document.transforms(layer);
        CHECK(chain.size() == 3);
        CHECK(std::holds_alternative<Scale>(chain[0]));
        CHECK(transform_list_text(chain) == "scale(2 2) translate(1 1) translate(2 3)");
    }

    // Undo moves the node back to the older head; redo finds the newer one again.
    document.undo();
    CHECK(transform_list_text(document.transforms(layer)) == "translate(1 1) translate(2 3)");
    document.redo();
    CHECK(transform_list_text(document.transforms(layer)) == "scale(2 2) translate(1 1) translate(2 3)");

    // A whole list from a file keeps every step and its order, including repeated kinds.
    const std::vector<Transform> written{Scale{2.0F, 2.0F}, Rotate{30.0F}, Scale{3.0F, 1.0F}, Rotate{45.0F}};
    {
        Transaction edit = document.transaction();
        edit.set_transforms(layer, written);
        edit.commit();
    }
    CHECK(transform_list_text(document.transforms(layer)) ==
          "scale(2 2) rotate(30) scale(3 1) rotate(45)");

    // A rolled-back edit leaves neither the node nor the chain behind.
    {
        Transaction edit = document.transaction();
        edit.prepend_transform(layer, Translate{user_units(9), user_units(9)});
    }
    CHECK(transform_list_text(document.transforms(layer)) ==
          "scale(2 2) rotate(30) scale(3 1) rotate(45)");

    {
        Transaction edit = document.transaction();
        edit.set_transforms(layer, {});
        edit.commit();
    }
    CHECK(document.transforms(layer).empty());

    if (failures != 0) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("transform chain tests passed");
    return 0;
}
