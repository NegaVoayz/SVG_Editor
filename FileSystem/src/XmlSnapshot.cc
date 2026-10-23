#include "XmlSnapshot.hh"

namespace Xml {

std::string_view local_name(std::string_view qualified) noexcept {
    const std::size_t colon = qualified.rfind(':');
    if (colon == std::string_view::npos) return qualified;
    return qualified.substr(colon + 1);
}

std::optional<std::reference_wrapper<const XmlAttribute>> attribute(const XmlNode& node, std::string_view name) noexcept {
    for (const XmlAttribute& candidate : node.attributes) {
        if (candidate.name == name) return std::cref(candidate);
    }
    return std::nullopt;
}

}  // namespace Xml
