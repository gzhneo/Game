/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Xml.h -- a small XML reader for the parts of .xlsx and .ods files, and escaping for writing.
//
// Builds a tree of elements and text nodes. Namespaces are not resolved: names keep their prefix and
// the readers compare local names (localName), since producers choose prefixes freely. No DTDs; at
// most 256 levels of nesting.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Babylon
{

struct XmlNode
{
	std::string name;		// empty for a text node
	std::vector<std::pair<std::string, std::string>> attributes;
	std::vector<XmlNode> children;
	std::string text;		// of a text node, entities resolved

	[[nodiscard]] bool isText() const { return name.empty(); }
	[[nodiscard]] std::string_view local() const;
	// Value of the first attribute with this local name; null if none.
	[[nodiscard]] const std::string *attribute(std::string_view localName) const;
	// First child element with this local name; null if none.
	[[nodiscard]] const XmlNode *child(std::string_view localName) const;
	// All text below this node, in order.
	[[nodiscard]] std::string allText() const;
};

std::string_view localName(std::string_view name);

// The root element; false with a message for malformed XML.
bool parseXml(std::string_view xml, XmlNode &root, std::string &error);

// Escapes &, <, > and quotes; characters XML 1.0 forbids are dropped.
std::string xmlEscape(std::string_view text);

} // namespace Babylon
