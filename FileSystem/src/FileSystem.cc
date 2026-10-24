#include "FileSystem.hh"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "Event.hh"
#include "ParsedSvg.hh"
#include "Refusal.hh"
#include "SvgDocument.hh"
#include "SvgReader.hh"
#include "SvgWriter.hh"
#include "TinyXml2Adapter.hh"

namespace {

std::optional<Container::SVG> canvas_of(const Document& document) {
    if (!document.has_document()) return std::nullopt;
    const std::optional<Node> payload = document.node(document.root());
    if (!payload || !std::holds_alternative<SVGNode>(*payload)) return std::nullopt;
    const SVGNode& element = std::get<SVGNode>(*payload);
    if (!std::holds_alternative<Container>(element.data)) return std::nullopt;
    const Container& container = std::get<Container>(element.data);
    if (!std::holds_alternative<Container::SVG>(container.specific)) return std::nullopt;
    return std::get<Container::SVG>(container.specific);
}

Outcome invalid_xml(const XmlReadError& error) {
    const SvgProblem problem =
        error.kind == XmlReadError::Kind::NoRoot ? SvgProblem::NoRootElement : SvgProblem::MalformedXml;
    return refused(Kind::Context,
                   fault::InvalidSvg{SvgDiagnostic{problem, error.line, {}, {}, {}, error.detail}});
}

StagedTree staged_tree(const ParsedSvg& parsed) {
    StagedTree staged;
    staged.root = parsed.root;
    staged.nodes.reserve(parsed.nodes.size());
    for (const ParsedNode& node : parsed.nodes) {
        staged.nodes.push_back(StagedNode{node.payload, node.children});
    }
    return staged;
}

}  // namespace

FileSystem::FileSystem(Document& document) : document_(document) {}

template<>
Outcome FileSystem::handle<Init>(const Init& intent) {
    const std::optional<LengthPercentage> width = present_length_percentage(intent.width);
    const std::optional<LengthPercentage> height = present_length_percentage(intent.height);
    if (width && !is_finite(*width)) {
        return refused(Kind::Context, fault::NonFiniteValue{"width"});
    }
    if (height && !is_finite(*height)) {
        return refused(Kind::Context, fault::NonFiniteValue{"height"});
    }
    if (width && numeric_value(*width) <= 0.0F) {
        return refused(Kind::Context, fault::NonPositiveCanvas{"width"});
    }
    if (height && numeric_value(*height) <= 0.0F) {
        return refused(Kind::Context, fault::NonPositiveCanvas{"height"});
    }

    document_.reset(intent.width, intent.height);
    source_path_.clear();
    const std::optional<Container::SVG> canvas = canvas_of(document_);
    if (!canvas) return refused(Kind::Context, fault::InternalError{});
    return happened(Kind::Context, event::Initialized{canvas->width, canvas->height});
}

template<>
Outcome FileSystem::handle<Read>(const Read& intent) {
    const FileResult file = file_system_detail::read(intent.path);
    if (std::holds_alternative<FileReadError>(file)) {
        const FileReadError& error = std::get<FileReadError>(file);
        return refused(Kind::Context, fault::CannotReadFile{error.path});
    }

    const FileBytes& bytes = std::get<FileBytes>(file);
    const XmlReadResult xml = read_xml(bytes.bytes);
    if (std::holds_alternative<XmlReadError>(xml)) return invalid_xml(std::get<XmlReadError>(xml));

    const SvgReadResult parsed = read_svg(std::get<XmlSnapshot>(xml));
    if (std::holds_alternative<SvgDiagnostic>(parsed)) {
        return refused(Kind::Context, fault::InvalidSvg{std::get<SvgDiagnostic>(parsed)});
    }

    const ParsedSvg& document = std::get<ParsedSvg>(parsed);
    document_.replace(staged_tree(document));
    source_path_ = intent.path;
    return happened(Kind::Context,
                    event::Opened{intent.path, document.assigned_ids, document_.element_count()});
}

template<>
Outcome FileSystem::handle<Save>(const Save& intent) {
    if (!document_.has_document()) return no_document(Kind::Context);

    const std::string target = intent.path ? *intent.path : source_path_;
    if (target.empty()) return refused(Kind::Context, fault::MissingSourcePath{});

    const std::string svg = write_svg(document_);
    const FileResult file = file_system_detail::write(target, svg);
    if (std::holds_alternative<FileWriteError>(file)) {
        const FileWriteError& error = std::get<FileWriteError>(file);
        return refused(Kind::Context, fault::CannotWriteFile{error.path});
    }

    // What was written is what the file says from here on, so the way back to the
    // state before it goes with the history that remembered it.
    document_.clear_history(true);
    return happened(Kind::Context, event::Saved{target, document_.element_count()});
}

Outcome FileSystem::translate(const FileIntent& intent) {
    try {
        return std::visit([this](const auto& value) { return handle(value); }, intent);
    } catch (const std::exception& error) {
        if (const char* debug = std::getenv("SVG_EDITOR_DEBUG"); debug != nullptr && *debug != '\0') {
            std::cerr << "internal file failure: " << error.what() << '\n';
        }
        return refused(Kind::Context, fault::InternalError{});
    }
}
