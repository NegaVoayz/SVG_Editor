#pragma once
#include <string>
#include <variant>
#include <optional>

#include "BasicType.hh"
#include "Attr.hh"

struct Shape {
    struct Rect { LengthPercentage x, y, w, h; };
    struct Circle { LengthPercentage cx, cy, r; };
    struct Ellipse { LengthPercentage cx, cy, rx, ry; };
    struct Line { LengthPercentage x1, y1, x2, y2; };
    std::variant<Rect,Circle,Ellipse,Line> specific;
};

struct PlainText {
    std::string content;
};

struct Text { 
    struct Common {
        std::optional<LengthPercentage> x, y, dx, dy;
    } common;
    
    ChildAttr child_attr;
    struct Wrapper {};
    struct Span {};
    struct Path { std::optional<LengthPercentage> startOffset; std::string href; };
    std::variant<Wrapper,Span,Path> specific;
};

struct Container {
    ChildAttr child_attr;

    struct Group {};
    struct Defs {};
    struct SVG {};
    struct Symbol {};
    struct Switch {};
    std::variant<Group, Defs, SVG, Symbol, Switch> specific;
};

struct SVGNode {
    IdentityAttr identity_attr;
    PaintAttr paint_attr;
    TransformAttr transform_attr;
    
    std::variant<Shape,Container,Text> data;
};

using Node = std::variant<SVGNode, PlainText>;
