#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "Color.hh"
#include "Document.hh"
#include "ElementName.hh"
#include "SvgFields.hh"
#include "SvgSchema.hh"
#include "TinyXml2Adapter.hh"

namespace {

int failures = 0;

void check(bool condition, const char* expression, int line) {
    if (condition) return;
    std::printf("FAIL line %d: %s\n", line, expression);
    ++failures;
}

std::filesystem::path file_named(std::string_view name) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "svg_editor_svg_io";
    std::filesystem::create_directories(directory);
    return directory / std::string{name};
}

bool write_text(const std::filesystem::path& path, std::string_view text) {
    std::ofstream file{path, std::ios::binary | std::ios::trunc};
    if (!file) return false;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    return file.good();
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream file{path, std::ios::binary};
    std::ostringstream text;
    text << file.rdbuf();
    return text.str();
}

std::optional<Id> named(const Document& document, std::string_view id) { return document.find(id); }

std::optional<SVGNode> element_at(const Document& document, Id id) {
    const std::optional<Node> payload = document.node(id);
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    return std::get<SVGNode>(*payload);
}

bool has_problem(const Status& status, SvgProblem problem) {
    const model_status::InvalidSvg* invalid = std::get_if<model_status::InvalidSvg>(&status);
    return invalid != nullptr && invalid->diagnostic.problem == problem;
}

std::optional<SvgDiagnostic> diagnostic_of(const Status& status) {
    if (const model_status::InvalidSvg* invalid = std::get_if<model_status::InvalidSvg>(&status)) {
        return invalid->diagnostic;
    }
    return std::nullopt;
}

bool near(float left, float right, float tolerance = 1.0e-4F) { return std::fabs(left - right) <= tolerance; }

constexpr std::string_view full_svg = R"SVG(<?xml version="1.0" encoding="UTF-8"?>
<!-- declaration and comments are syntax, not tree nodes -->
<svg xmlns="http://www.w3.org/2000/svg" width="320mm" height="200mm" viewBox="0 0 320 200">
  <g id="layer" fill="RED" stroke="url(#paint) red" transform="translate(1 2) scale(2) rotate(30 10 20) skewX(15) skewY(-5)">
    <rect x="1" y="2" width="30" height="40" fill="#3366ff"/>
    <circle cx="50" cy="60" r="10" stroke="darkslategrey" stroke-width="2"/>
    <ellipse cx="70" cy="80" rx="12" ry="8"/>
    <line x1="0" y1="0" x2="20" y2="30" stroke="rgb(1, 2, 3)"/>
  </g>
  <defs><symbol id="mark"/></defs>
  <switch><g id="fallback"/></switch>
  <text x="20" y="40" text-anchor="middle" font-size="12pt">Hello &amp; &lt;SVG&gt;<tspan id="small" fill="rebeccapurple"> again</tspan><textPath id="along" xlink:href="#mark" startOffset="10%">path</textPath></text>
</svg>)SVG";

}  // namespace

#define CHECK(condition) check((condition), #condition, __LINE__)

int main() {
    // The reader and writer validate attributes through the same table.
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::common_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::svg_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::group_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::rect_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::circle_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::ellipse_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::line_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::text_common_fields));
    CHECK(svg_fields::has_unique_attribute_names(svg_fields::text_path_attributes));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::svg_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::group_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::rect_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::circle_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::ellipse_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::line_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::text_common_fields));
    CHECK(svg_fields::disjoint_attribute_names(svg_fields::common_fields, svg_fields::text_path_attributes));
    CHECK(svg_schema::is_element("rect"));
    CHECK(svg_schema::is_element("svg"));
    CHECK(svg_schema::is_element("switch"));
    CHECK(svg_schema::is_element("textPath"));
    CHECK(!svg_schema::is_element("path"));
    CHECK(svg_schema::accepts_attribute("rect", "x"));
    CHECK(svg_schema::accepts_attribute("rect", "fill"));
    CHECK(svg_schema::accepts_attribute("svg", "viewBox"));
    CHECK(svg_schema::accepts_attribute("g", "class"));
    CHECK(svg_schema::accepts_attribute("g", "data-kind"));
    CHECK(!svg_schema::accepts_attribute("rect", "viewBox"));
    CHECK(svg_schema::accepts_attribute("text", "font-size"));
    CHECK(svg_schema::accepts_attribute("textPath", "startOffset"));
    CHECK(svg_schema::accepts_attribute("textPath", "xlink:href"));
    CHECK(!svg_schema::accepts_attribute("g", "text-anchor"));
    CHECK(!svg_schema::accepts_attribute("textPath", "transform-origin"));
    const svg_schema::ElementSchema* root_schema = svg_schema::schema_for("svg");
    CHECK(root_schema != nullptr);
    if (root_schema) CHECK(root_schema->family == svg_schema::ElementFamily::Container);

    // The enum and its spelling table are generated from the same definition list, and
    // every canonical spelling reads back as the same enum value.
    CHECK(named_color_count == 148);
    for (std::size_t index = 0; index < named_color_count; ++index) {
        const Color::NamedColorId id = static_cast<Color::NamedColorId>(index);
        const std::string_view text = named_color_text(id);
        CHECK(!text.empty());
        CHECK(named_color_of(text) == id);
    }
    CHECK(named_color_of("RebeccaPurple") == Color::NamedColorId::RebeccaPurple);
    CHECK(named_color_of("AQUA") == Color::NamedColorId::Aqua);
    CHECK(named_color_of("cyan") == Color::NamedColorId::Cyan);
    CHECK(std::holds_alternative<Color::Named>(Color{Color::Named{Color::NamedColorId::Red}}.specific));
    CHECK(to_text(Color{Color::Named{Color::NamedColorId::RebeccaPurple}}) == "rebeccapurple");
    CHECK(to_text(Color{Color::System{Color::SystemColorId::CanvasText}}) == "canvastext");
    CHECK(to_text(Color{Color::RGB{0x12, 0x34, 0xab}}) == "#1234ab");
    CHECK(to_text(Color{Color::FuncRGB{1, 2, 3}}) == "rgb(1,2,3)");
    CHECK(to_text(Paint{PaintServer{"url(#paint)", Color{Color::Named{Color::NamedColorId::Red}}}}) ==
          "url(#paint) red");

    // The adapter owns every tinyxml2 pointer. Its result is a stable value snapshot.
    {
        const XmlReadResult parsed = read_xml(
            "<?xml version=\"1.0\"?><!-- ignored --><root a=\"1\"><child>text &amp; <![CDATA[more]]></child></root>");
        CHECK(std::holds_alternative<XmlSnapshot>(parsed));
        if (const XmlSnapshot* snapshot = std::get_if<XmlSnapshot>(&parsed)) {
            CHECK(snapshot->nodes[snapshot->root].name == "root");
            CHECK(snapshot->nodes[snapshot->root].attributes.size() == 1);
            const std::vector<std::size_t>& children = snapshot->nodes[snapshot->root].children;
            CHECK(children.size() == 1);
            if (children.size() == 1) {
                const XmlNode& child = snapshot->nodes[children.front()];
                CHECK(child.name == "child");
                std::string text;
                for (const std::size_t part : child.children) {
                    text += snapshot->nodes[part].text;
                }
                CHECK(text == "text & more");
            }
        }
    }
    {
        const XmlReadResult malformed = read_xml("<root>");
        CHECK(std::holds_alternative<XmlReadError>(malformed));
        if (const XmlReadError* error = std::get_if<XmlReadError>(&malformed)) {
            CHECK(error->kind == XmlReadError::Kind::Malformed);
            CHECK(error->line != 0);
        }
    }
    {
        const XmlReadResult empty = read_xml("  \n");
        CHECK(std::holds_alternative<XmlReadError>(empty));
        if (const XmlReadError* error = std::get_if<XmlReadError>(&empty)) {
            CHECK(error->kind == XmlReadError::Kind::NoRoot);
        }
    }

    const std::filesystem::path full_path = file_named("svg_io_full.svg");
    CHECK(write_text(full_path, full_svg));

    Document document;
    const Status opened = document.open(full_path.string());
    CHECK(ok(opened));
    const model_status::Opened* open_success = std::get_if<model_status::Opened>(&opened);
    CHECK(open_success != nullptr);
    if (open_success) {
        CHECK(open_success->assigned_ids.size() == 7);
        CHECK(open_success->assigned_ids.front().id == "rect1");
        CHECK(open_success->assigned_ids.front().element == "rect");
    }
    CHECK(document.source_path() == full_path.string());
    CHECK(document.depth() == 0);

    // An empty transform attribute is the same as no transform, not a malformed value.
    const std::filesystem::path empty_transform_path = file_named("svg_io_empty_transform.svg");
    CHECK(write_text(empty_transform_path,
                     R"(<svg><g id="plain" transform=""/><g id="spaced" transform="   "/></svg>)"));
    Document empty_transform_document;
    CHECK(ok(empty_transform_document.open(empty_transform_path.string())));
    const std::optional<Id> plain_group = named(empty_transform_document, "plain");
    const std::optional<Id> spaced_group = named(empty_transform_document, "spaced");
    CHECK(plain_group.has_value());
    CHECK(spaced_group.has_value());
    if (plain_group) CHECK(is_identity(empty_transform_document.transform(*plain_group)));
    if (spaced_group) CHECK(is_identity(empty_transform_document.transform(*spaced_group)));

    const std::optional<Id> root = named(document, "");
    CHECK(!root.has_value());
    if (const std::optional<SVGNode> svg = element_at(document, document.root())) {
        CHECK(svg->identity_attr.id.empty());
        const Container& container = std::get<Container>(svg->data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        CHECK(canvas.width && to_text(*canvas.width) == "320mm");
        CHECK(canvas.height && to_text(*canvas.height) == "200mm");
        CHECK(canvas.xmlns == "http://www.w3.org/2000/svg");
        CHECK(canvas.view_box.has_value());
        if (canvas.view_box) {
            CHECK(canvas.view_box->min_x == 0.0F);
            CHECK(canvas.view_box->min_y == 0.0F);
            CHECK(canvas.view_box->width == 320.0F);
            CHECK(canvas.view_box->height == 200.0F);
        }
    }

    const std::optional<Id> layer = named(document, "layer");
    CHECK(layer.has_value());
    if (layer) {
        const std::optional<SVGNode> payload = element_at(document, *layer);
        CHECK(payload.has_value());
        if (payload) {
            CHECK(payload->paint_attr.fill.has_value());
            if (payload->paint_attr.fill) {
                CHECK(std::holds_alternative<Color>(*payload->paint_attr.fill));
                const Color& fill = std::get<Color>(*payload->paint_attr.fill);
                CHECK(std::holds_alternative<Color::Named>(fill.specific));
                CHECK(std::get<Color::Named>(fill.specific).id == Color::NamedColorId::Red);
            }
            CHECK(payload->paint_attr.stroke.has_value());
            if (payload->paint_attr.stroke) {
                CHECK(std::holds_alternative<PaintServer>(*payload->paint_attr.stroke));
                if (std::holds_alternative<PaintServer>(*payload->paint_attr.stroke)) {
                    const PaintServer& server = std::get<PaintServer>(*payload->paint_attr.stroke);
                    CHECK(server.url == "url(#paint)");
                    CHECK(server.fallback.has_value());
                }
            }
            const Transform transform = document.transform(*layer);
            CHECK(!is_identity(transform));
            CHECK(near(transform.a, 1.7789358F));
            CHECK(near(transform.b, 0.8250227F));
            CHECK(near(transform.c, -0.5358984F));
            CHECK(near(transform.d, 2.0F));
            CHECK(near(numeric_value(transform.e), 23.67949F, 1.0e-3F));
            CHECK(near(numeric_value(transform.f), -2.641016F, 1.0e-3F));
            CHECK(document.children(*layer).size() == 4);
        }
    }

    CHECK(named(document, "rect1").has_value());
    CHECK(named(document, "circle1").has_value());
    CHECK(named(document, "ellipse1").has_value());
    CHECK(named(document, "line1").has_value());
    CHECK(named(document, "defs1").has_value());
    CHECK(named(document, "switch1").has_value());
    CHECK(named(document, "text1").has_value());
    CHECK(named(document, "small").has_value());
    CHECK(named(document, "along").has_value());

    // Display and serialization share one exhaustive element-name mapping.
    if (const std::optional<Id> rect1 = named(document, "rect1")) {
        if (const std::optional<SVGNode> payload = element_at(document, *rect1)) CHECK(element_name(*payload) == "rect");
    }
    if (const std::optional<Id> layer_node = named(document, "layer")) {
        if (const std::optional<SVGNode> payload = element_at(document, *layer_node)) CHECK(element_name(*payload) == "g");
    }
    if (const std::optional<Id> span = named(document, "small")) {
        if (const std::optional<SVGNode> payload = element_at(document, *span)) CHECK(element_name(*payload) == "tspan");
    }
    if (const std::optional<Id> path = named(document, "along")) {
        if (const std::optional<SVGNode> payload = element_at(document, *path)) {
            CHECK(element_name(*payload) == "textPath");
        }
    }

    const std::optional<Id> text = named(document, "text1");
    CHECK(text.has_value());
    if (text) {
        std::string content;
        for (const Id child : document.children(*text)) {
            if (const std::optional<Node> payload = document.node(child);
                payload && std::holds_alternative<PlainText>(*payload)) {
                content += std::get<PlainText>(*payload).content;
            }
        }
        CHECK(content == "Hello & <SVG>");
        const std::optional<SVGNode> payload = element_at(document, *text);
        CHECK(payload.has_value());
        if (payload && std::holds_alternative<Text>(payload->data)) {
            const Text& value = std::get<Text>(payload->data);
            CHECK(value.common.text_anchor == Text::Anchor::Middle);
            CHECK(value.common.font_size.has_value());
            if (value.common.font_size) CHECK(to_text(*value.common.font_size) == "12pt");
        }
    }

    // An unnamed node must not take an id a later explicit node already has.
    const std::filesystem::path collision_path = file_named("svg_io_collision.svg");
    CHECK(write_text(collision_path,
                     R"(<svg><rect width="1" height="1"/><rect id="rect1" width="2" height="2"/><rect width="3" height="3"/></svg>)"));
    Document collision;
    const Status collision_status = collision.open(collision_path.string());
    CHECK(ok(collision_status));
    if (const model_status::Opened* warnings = std::get_if<model_status::Opened>(&collision_status)) {
        CHECK(warnings->assigned_ids.size() == 2);
        CHECK(warnings->assigned_ids[0].id == "rect2");
        CHECK(warnings->assigned_ids[1].id == "rect3");
    }
    CHECK(collision.find("rect2").has_value());
    CHECK(collision.find("rect3").has_value());

    // `svg` names the root, so a non-root element carrying it is renamed. Two such
    // elements are not duplicates of each other, and a generated id still avoids the
    // explicit ids the file already holds.
    const std::filesystem::path reserved_path = file_named("svg_io_reserved.svg");
    CHECK(write_text(reserved_path,
                     R"(<svg><rect id="svg" width="1" height="1"/><rect id="rect1" width="2" height="2"/><g id="svg"><circle r="1"/></g></svg>)"));
    Document reserved;
    const Status reserved_status = reserved.open(reserved_path.string());
    CHECK(ok(reserved_status));
    if (const model_status::Opened* renamed = std::get_if<model_status::Opened>(&reserved_status)) {
        CHECK(renamed->assigned_ids.size() == 3);
        if (renamed->assigned_ids.size() == 3) {
            CHECK(renamed->assigned_ids[0].id == "rect2");
            CHECK(renamed->assigned_ids[0].original_id == "svg");
            CHECK(renamed->assigned_ids[1].id == "g1");
            CHECK(renamed->assigned_ids[1].original_id == "svg");
            CHECK(renamed->assigned_ids[2].id == "circle1");
            CHECK(renamed->assigned_ids[2].original_id.empty());
        }
    }
    CHECK(!reserved.find("svg").has_value());
    CHECK(reserved.find("rect2").has_value());
    CHECK(reserved.find("g1").has_value());
    CHECK(reserved.find("circle1").has_value());

    // Every rejected file leaves the current tree and source path exactly as they were.
    const Status before_failure_tree = collision.save(file_named("svg_io_before_failure.svg").string());
    CHECK(ok(before_failure_tree));
    const std::string preserved_path = collision.source_path();
    const std::size_t preserved_children = collision.children(collision.root()).size();
    const std::vector<std::string> rejected{
        R"(<svg><path/></svg>)",
        R"(<svg viewBox="0 0 -10 10"/>)",
        R"(<svg><rect style="fill:red"/></svg>)",
        R"(<svg><rect width="wide" height="2"/></svg>)",
        R"(<svg><g>words</g></svg>)",
        R"(<svg><g id="same"/><rect id="same"/></svg>)",
        R"(<svg><g id="a b"/></svg>)",
        R"(<svg><rect id="1bad"/></svg>)",
        R"(<not-svg/>)",
    };
    for (std::size_t index = 0; index < rejected.size(); ++index) {
        const std::filesystem::path path = file_named("svg_io_rejected_" + std::to_string(index) + ".svg");
        CHECK(write_text(path, rejected[index]));
        const Status rejected_status = collision.open(path.string());
        CHECK(!ok(rejected_status));
        CHECK(diagnostic_of(rejected_status).has_value());
        CHECK(collision.source_path() == preserved_path);
        CHECK(collision.children(collision.root()).size() == preserved_children);
    }

    const std::filesystem::path malformed_path = file_named("svg_io_malformed.svg");
    CHECK(write_text(malformed_path, "<svg>"));
    CHECK(has_problem(collision.open(malformed_path.string()), SvgProblem::MalformedXml));

    const std::filesystem::path invalid_id_path = file_named("svg_io_invalid_id.svg");
    CHECK(write_text(invalid_id_path, R"(<svg><g id="a b"/></svg>)"));
    CHECK(has_problem(collision.open(invalid_id_path.string()), SvgProblem::InvalidId));

    // A successful read and save round-trip preserves structure, values, text and the
    // ids that were assigned while reading.
    const std::filesystem::path saved_path = file_named("svg_io_saved.svg");
    CHECK(ok(document.save(saved_path.string())));
    CHECK(document.depth() == 0);
    CHECK(!document.can_undo());
    const std::string saved_svg = read_text(saved_path);
    CHECK(saved_svg.find("href=\"#mark\"") != std::string::npos);
    CHECK(saved_svg.find("xlink:href") == std::string::npos);

    Document reread;
    CHECK(ok(reread.open(saved_path.string())));
    CHECK(named(reread, "rect1").has_value());
    CHECK(named(reread, "small").has_value());
    if (const std::optional<SVGNode> root = element_at(reread, reread.root())) {
        const Container& container = std::get<Container>(root->data);
        const Container::SVG& canvas = std::get<Container::SVG>(container.specific);
        CHECK(canvas.xmlns == "http://www.w3.org/2000/svg");
        CHECK(canvas.view_box.has_value());
        if (canvas.view_box) CHECK(to_text(*canvas.view_box) == "0 0 320 200");
    }
    const std::optional<Id> saved_text = named(reread, "text1");
    CHECK(saved_text.has_value());
    if (saved_text) {
        const std::optional<SVGNode> payload = element_at(reread, *saved_text);
        CHECK(payload.has_value());
        if (payload && std::holds_alternative<Text>(payload->data)) {
            const Text& value = std::get<Text>(payload->data);
            CHECK(value.common.text_anchor == Text::Anchor::Middle);
            CHECK(value.common.font_size.has_value());
            if (value.common.font_size) CHECK(to_text(*value.common.font_size) == "12pt");
        }
    }
    const std::optional<Id> saved_layer = named(reread, "layer");
    CHECK(saved_layer.has_value());
    if (saved_layer) {
        const std::optional<SVGNode> payload = element_at(reread, *saved_layer);
        CHECK(payload.has_value());
        if (payload && payload->paint_attr.fill) {
            const Color& fill = std::get<Color>(*payload->paint_attr.fill);
            CHECK(std::holds_alternative<Color::Named>(fill.specific));
            CHECK(std::get<Color::Named>(fill.specific).id == Color::NamedColorId::Red);
        }
        const Transform saved_transform = reread.transform(*saved_layer);
        CHECK(!is_identity(saved_transform));
        CHECK(near(saved_transform.a, 1.7789358F));
        CHECK(near(saved_transform.b, 0.8250227F));
        CHECK(near(saved_transform.c, -0.5358984F));
        CHECK(near(saved_transform.d, 2.0F));
        CHECK(near(numeric_value(saved_transform.e), 23.67949F, 1.0e-3F));
        CHECK(near(numeric_value(saved_transform.f), -2.641016F, 1.0e-3F));
    }
    const std::optional<Id> saved_small = named(reread, "small");
    CHECK(saved_small.has_value());
    if (saved_small) {
        const std::optional<SVGNode> payload = element_at(reread, *saved_small);
        CHECK(payload.has_value());
        if (payload && payload->paint_attr.fill) {
            const Color& fill = std::get<Color>(*payload->paint_attr.fill);
            CHECK(std::holds_alternative<Color::Named>(fill.specific));
            CHECK(std::get<Color::Named>(fill.specific).id == Color::NamedColorId::RebeccaPurple);
        }
    }

    // A composite kind keeps its marker through a save and read, while the file remains
    // an ordinary SVG group for consumers that do not know the kind.
    const std::filesystem::path composite_path = file_named("svg_io_composite_kind.svg");
    CHECK(write_text(composite_path,
                     R"SVG(<svg><g id="status" data-kind="box"><rect id="status-rect" width="10" height="10"/></g></svg>)SVG"));
    Document composite_document;
    const Status composite_opened = composite_document.open(composite_path.string());
    CHECK(ok(composite_opened));
    if (ok(composite_opened)) {
        CHECK(ok(composite_document.save(composite_path.string())));
        CHECK(read_text(composite_path).find("<g id=\"status\" data-kind=\"box\">") != std::string::npos);
        Document composite_reread;
        CHECK(ok(composite_reread.open(composite_path.string())));
        const std::optional<Id> composite = named(composite_reread, "status");
        CHECK(composite.has_value());
        if (composite) {
            const std::optional<SVGNode> payload = element_at(composite_reread, *composite);
            CHECK(payload.has_value());
            if (payload && std::holds_alternative<Container>(payload->data)) {
                const Container& container = std::get<Container>(payload->data);
                CHECK(std::holds_alternative<Container::Group>(container.specific));
                if (std::holds_alternative<Container::Group>(container.specific)) {
                    const std::string& kind = std::get<Container::Group>(container.specific).data_kind;
                    CHECK(kind == "box");
                }
            }
        }
    }

    // The file and the user read the same TRS+skew form: the matrix a file writes becomes
    // translate(...) rotate(...) scale(...) skewX(...), with identity steps left out.
    const std::filesystem::path trs_path = file_named("svg_io_transform_trs.svg");
    CHECK(write_text(trs_path,
                     R"SVG(<svg><rect id="turned" width="10" height="10" transform="matrix(0 1 -1 0 10 0)"/></svg>)SVG"));
    Document trs_document;
    CHECK(ok(trs_document.open(trs_path.string())));
    CHECK(ok(trs_document.save(trs_path.string())));
    const std::string trs_svg = read_text(trs_path);
    CHECK(trs_svg.find("transform=\"translate(10 0) rotate(90)\"") != std::string::npos);

    // A skew that already has the shortest form stays that way when it is written back.
    const std::filesystem::path skew_path = file_named("svg_io_transform_skew.svg");
    CHECK(write_text(skew_path,
                     R"SVG(<svg><rect id="sheared" width="10" height="10" transform="skewY(15)"/></svg>)SVG"));
    Document skew_document;
    CHECK(ok(skew_document.open(skew_path.string())));
    CHECK(ok(skew_document.save(skew_path.string())));
    CHECK(read_text(skew_path).find("transform=\"skewY(15)\"") != std::string::npos);

    // Small non-zero components are real geometry, not identity noise.
    const std::filesystem::path micro_transform_path = file_named("svg_io_micro_transform.svg");
    CHECK(write_text(micro_transform_path,
                     R"SVG(<svg><rect id="moved" width="1" height="1" transform="translate(0.000001 0)"/><rect id="turned" width="1" height="1" transform="rotate(0.0005)"/></svg>)SVG"));
    Document micro_transform_document;
    CHECK(ok(micro_transform_document.open(micro_transform_path.string())));
    CHECK(ok(micro_transform_document.save(micro_transform_path.string())));
    const std::string micro_transform_svg = read_text(micro_transform_path);
    CHECK(micro_transform_svg.find("transform=\"translate(1e-06 0)\"") != std::string::npos);
    CHECK(micro_transform_svg.find("transform=\"rotate(") != std::string::npos);
    Document micro_transform_reread;
    CHECK(ok(micro_transform_reread.open(micro_transform_path.string())));
    const std::optional<Id> moved = named(micro_transform_reread, "moved");
    const std::optional<Id> turned = named(micro_transform_reread, "turned");
    CHECK(moved.has_value() && turned.has_value());
    if (moved) CHECK(std::get<Length>(micro_transform_reread.transform(*moved).e).value == 1.0e-6F);
    if (turned) CHECK(micro_transform_reread.transform(*turned).b != 0.0F);

    // Serialization keeps a sub-micro value instead of rounding it to zero.
    const std::filesystem::path tiny_path = file_named("svg_io_tiny_number.svg");
    CHECK(write_text(
        tiny_path,
        R"(<svg><rect id="tiny" x="0.0000004" y="-0" width="1" height="1" stroke-width="0.00000045"/></svg>)"));
    Document tiny_document;
    CHECK(ok(tiny_document.open(tiny_path.string())));
    CHECK(ok(tiny_document.save(tiny_path.string())));
    const std::string tiny_svg = read_text(tiny_path);
    CHECK(tiny_svg.find("x=\"" + exact_number_text(4.0e-7F) + "\"") != std::string::npos);
    CHECK(tiny_svg.find("y=\"0\"") != std::string::npos);
    CHECK(tiny_svg.find("stroke-width=\"" + exact_number_text(4.5e-7F) + "\"") != std::string::npos);
    Document tiny_reread;
    CHECK(ok(tiny_reread.open(tiny_path.string())));
    const std::optional<Id> tiny = named(tiny_reread, "tiny");
    CHECK(tiny.has_value());
    if (tiny) {
        const std::optional<SVGNode> tiny_element = element_at(tiny_reread, *tiny);
        CHECK(tiny_element.has_value());
        if (tiny_element && std::holds_alternative<Shape>(tiny_element->data)) {
            const Shape::Rect& rect = std::get<Shape::Rect>(std::get<Shape>(tiny_element->data).specific);
            CHECK(std::get<Length>(rect.x).value == 4.0e-7F);
            CHECK(std::get<Length>(rect.y).value == 0.0F);
            CHECK(tiny_element->paint_attr.stroke_width.has_value());
            if (tiny_element->paint_attr.stroke_width) {
                CHECK(std::get<Length>(*tiny_element->paint_attr.stroke_width).value == 4.5e-7F);
            }
        }
    }

    if (failures != 0) {
        std::printf("svg_io: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("svg_io: all checks passed\n");
    return 0;
}
