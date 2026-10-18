#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

// A colour the model can carry without losing what the SVG source said. Named and
// system colours keep their identity rather than being reduced to RGB, so a load and a
// save can preserve the spelling the document chose.
struct Color {
    enum class SystemColorId : uint32_t {
        AccentColor,
        AccentColorText,
        ActiveText,
        ButtonBorder,
        ButtonFace,
        ButtonText,
        Canvas,
        CanvasText,
        Field,
        FieldText,
        GrayText,
        Highlight,
        HighlightText,
        LinkText,
        Mark,
        MarkText,
        SelectedItem,
        SelectedItemText,
        VisitedText
    };

    enum class NamedColorId : uint32_t {
#define SVG_EDITOR_NAMED_COLOR(name, text) name,
#include "NamedColors.def"
#undef SVG_EDITOR_NAMED_COLOR
    };

    struct None {};
    struct Current {};
    struct System {
        SystemColorId id;
    };
    struct Named {
        NamedColorId id;
    };
    struct RGB {
        uint8_t r, g, b;
    };
    struct FuncRGB {
        uint8_t r, g, b;
    };

    std::variant<None, Current, System, Named, RGB, FuncRGB> specific;
};

struct PaintServer {
    std::string url;
    std::optional<Color> fallback;
};

using Paint = std::variant<Color, PaintServer>;

// The number of CSS Color 4 names, and the spelling SVG writes for each one.
inline constexpr std::size_t named_color_count = 148;
std::string_view named_color_text(Color::NamedColorId id) noexcept;
std::optional<Color::NamedColorId> named_color_of(std::string_view text) noexcept;

std::string_view system_color_text(Color::SystemColorId id) noexcept;
std::optional<Color::SystemColorId> system_color_of(std::string_view text) noexcept;

// The canonical SVG spelling of a value: named and system colours keep their names,
// hex colours become #rrggbb, and function colours use the compact rgb(...) form.
std::string to_text(const Color& color);
std::string to_text(const Paint& paint);
