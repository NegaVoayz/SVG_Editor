#pragma once

#include <variant>

#include "ParsedSvg.hh"
#include "SvgDiagnostic.hh"
#include "XmlSnapshot.hh"

using SvgReadResult = std::variant<ParsedSvg, SvgDiagnostic>;

// XML snapshot in, a staged SVG tree or a structured diagnostic out. It never touches
// Document, which is what makes a failed read atomic.
SvgReadResult read_svg(const XmlSnapshot& xml);
