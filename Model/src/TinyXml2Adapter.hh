#pragma once

#include <string_view>

#include "XmlSnapshot.hh"

// XML syntax in, a pointer-free snapshot out. This is the only boundary where
// tinyxml2 exists in the project.
XmlReadResult read_xml(std::string_view bytes);
