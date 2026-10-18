#include "Color.hh"

#include <array>
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace {

struct NamedColorText {
    Color::NamedColorId id;
    std::string_view text;
};

constexpr std::array named_colors{
#define SVG_EDITOR_NAMED_COLOR(name, text) NamedColorText{Color::NamedColorId::name, text},
#include "NamedColors.def"
#undef SVG_EDITOR_NAMED_COLOR
};
static_assert(named_colors.size() == named_color_count);

struct SystemColorText {
    Color::SystemColorId id;
    std::string_view text;
};

constexpr std::array system_colors{
    SystemColorText{Color::SystemColorId::AccentColor, "accentcolor"},
    SystemColorText{Color::SystemColorId::AccentColorText, "accentcolortext"},
    SystemColorText{Color::SystemColorId::ActiveText, "activetext"},
    SystemColorText{Color::SystemColorId::ButtonBorder, "buttonborder"},
    SystemColorText{Color::SystemColorId::ButtonFace, "buttonface"},
    SystemColorText{Color::SystemColorId::ButtonText, "buttontext"},
    SystemColorText{Color::SystemColorId::Canvas, "canvas"},
    SystemColorText{Color::SystemColorId::CanvasText, "canvastext"},
    SystemColorText{Color::SystemColorId::Field, "field"},
    SystemColorText{Color::SystemColorId::FieldText, "fieldtext"},
    SystemColorText{Color::SystemColorId::GrayText, "graytext"},
    SystemColorText{Color::SystemColorId::Highlight, "highlight"},
    SystemColorText{Color::SystemColorId::HighlightText, "highlighttext"},
    SystemColorText{Color::SystemColorId::LinkText, "linktext"},
    SystemColorText{Color::SystemColorId::Mark, "mark"},
    SystemColorText{Color::SystemColorId::MarkText, "marktext"},
    SystemColorText{Color::SystemColorId::SelectedItem, "selecteditem"},
    SystemColorText{Color::SystemColorId::SelectedItemText, "selecteditemtext"},
    SystemColorText{Color::SystemColorId::VisitedText, "visitedtext"},
};

std::string ascii_lower(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::string hex_text(uint8_t value) {
    constexpr char digits[] = "0123456789abcdef";
    return std::string{digits[value / 16], digits[value % 16]};
}

struct ColorText {
    std::string operator()(const Color::None&) const { return "none"; }
    std::string operator()(const Color::Current&) const { return "currentColor"; }
    std::string operator()(const Color::System& system) const {
        return std::string{system_color_text(system.id)};
    }
    std::string operator()(const Color::Named& named) const { return std::string{named_color_text(named.id)}; }
    std::string operator()(const Color::RGB& rgb) const {
        return "#" + hex_text(rgb.r) + hex_text(rgb.g) + hex_text(rgb.b);
    }
    std::string operator()(const Color::FuncRGB& rgb) const {
        return "rgb(" + std::to_string(rgb.r) + "," + std::to_string(rgb.g) + "," + std::to_string(rgb.b) + ")";
    }
};

}  // namespace

std::string_view named_color_text(Color::NamedColorId id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= named_colors.size()) return {};
    return named_colors[index].text;
}

std::optional<Color::NamedColorId> named_color_of(std::string_view text) noexcept {
    const std::string lowered = ascii_lower(text);
    for (const NamedColorText& entry : named_colors) {
        if (entry.text == lowered) return entry.id;
    }
    return std::nullopt;
}

std::string_view system_color_text(Color::SystemColorId id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    if (index >= system_colors.size()) return {};
    return system_colors[index].text;
}

std::optional<Color::SystemColorId> system_color_of(std::string_view text) noexcept {
    const std::string lowered = ascii_lower(text);
    for (const SystemColorText& entry : system_colors) {
        if (entry.text == lowered) return entry.id;
    }
    return std::nullopt;
}

std::string to_text(const Color& color) { return std::visit(ColorText{}, color.specific); }

std::string to_text(const Paint& paint) {
    if (std::holds_alternative<Color>(paint)) return to_text(std::get<Color>(paint));
    const PaintServer& server = std::get<PaintServer>(paint);
    if (!server.fallback) return server.url;
    return server.url + " " + to_text(*server.fallback);
}
