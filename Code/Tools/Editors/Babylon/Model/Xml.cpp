/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Model/Xml.h"

#include <cstdlib>

namespace Babylon
{

namespace
{

// Spreadsheet parts nest a dozen levels; the limit keeps the recursive walks over the tree safe.
const std::size_t MAX_DEPTH = 256;

bool isNameChar(char c)
{
	return !(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '/' || c == '>' || c == '=' || c == '<' || c == '"' || c == '\'');
}

bool isBlank(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

void appendUtf8(std::string &out, unsigned long c)
{
	if (c < 0x80)
		out.push_back(static_cast<char>(c));
	else if (c < 0x800)
	{
		out.push_back(static_cast<char>(0xC0 | (c >> 6)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else if (c < 0x10000)
	{
		out.push_back(static_cast<char>(0xE0 | (c >> 12)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
	else if (c < 0x110000)
	{
		out.push_back(static_cast<char>(0xF0 | (c >> 18)));
		out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
		out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
	}
}

bool decodeEntities(std::string_view in, std::string &out)
{
	out.clear();
	out.reserve(in.size());
	for (std::size_t i = 0; i < in.size(); ++i)
	{
		if (in[i] != '&')
		{
			out.push_back(in[i]);
			continue;
		}
		const std::size_t semi = in.find(';', i);
		if (semi == std::string_view::npos || semi - i > 12)
			return false;
		const std::string_view e = in.substr(i + 1, semi - i - 1);
		if (e == "lt")
			out.push_back('<');
		else if (e == "gt")
			out.push_back('>');
		else if (e == "amp")
			out.push_back('&');
		else if (e == "quot")
			out.push_back('"');
		else if (e == "apos")
			out.push_back('\'');
		else if (e.size() > 1 && e[0] == '#')
		{
			const std::string digits(e.substr(e[1] == 'x' || e[1] == 'X' ? 2 : 1));
			char *end = nullptr;
			const unsigned long c = std::strtoul(digits.c_str(), &end, (e[1] == 'x' || e[1] == 'X') ? 16 : 10);
			if (digits.empty() || *end != 0)
				return false;
			appendUtf8(out, c);
		}
		else
			return false;
		i = semi;
	}
	return true;
}

} // namespace

std::string_view localName(std::string_view name)
{
	const std::size_t colon = name.find(':');
	return colon == std::string_view::npos ? name : name.substr(colon + 1);
}

std::string_view XmlNode::local() const
{
	return localName(name);
}

const std::string *XmlNode::attribute(std::string_view wanted) const
{
	for (const auto &a : attributes)
		if (localName(a.first) == wanted)
			return &a.second;
	return nullptr;
}

const XmlNode *XmlNode::child(std::string_view wanted) const
{
	for (const XmlNode &c : children)
		if (!c.isText() && c.local() == wanted)
			return &c;
	return nullptr;
}

std::string XmlNode::allText() const
{
	if (isText())
		return text;
	std::string s;
	for (const XmlNode &c : children)
		s += c.allText();
	return s;
}

bool parseXml(std::string_view xml, XmlNode &root, std::string &error)
{
	root = XmlNode();
	std::vector<XmlNode *> stack;
	bool haveRoot = false;
	std::size_t i = 0;
	if (xml.size() >= 3 && xml.substr(0, 3) == "\xEF\xBB\xBF")
		i = 3;
	const auto fail = [&error](const std::string &message) {
		error = message;
		return false;
	};

	while (i < xml.size())
	{
		if (xml[i] != '<')
		{
			const std::size_t lt = xml.find('<', i);
			const std::size_t end = lt == std::string_view::npos ? xml.size() : lt;
			if (!stack.empty())
			{
				XmlNode t;
				if (!decodeEntities(xml.substr(i, end - i), t.text))
					return fail("bad entity in text");
				stack.back()->children.push_back(std::move(t));
			}
			i = end;
			continue;
		}
		if (xml.compare(i, 4, "<!--") == 0)
		{
			const std::size_t end = xml.find("-->", i + 4);
			if (end == std::string_view::npos)
				return fail("comment not closed");
			i = end + 3;
			continue;
		}
		if (xml.compare(i, 9, "<![CDATA[") == 0)
		{
			const std::size_t end = xml.find("]]>", i + 9);
			if (end == std::string_view::npos)
				return fail("CDATA not closed");
			if (!stack.empty())
			{
				XmlNode t;
				t.text = std::string(xml.substr(i + 9, end - i - 9));
				stack.back()->children.push_back(std::move(t));
			}
			i = end + 3;
			continue;
		}
		if (xml.compare(i, 2, "<?") == 0)
		{
			const std::size_t end = xml.find("?>", i + 2);
			if (end == std::string_view::npos)
				return fail("processing instruction not closed");
			i = end + 2;
			continue;
		}
		if (xml.compare(i, 2, "<!") == 0)
		{
			const std::size_t end = xml.find('>', i + 2);
			if (end == std::string_view::npos)
				return fail("declaration not closed");
			i = end + 1;
			continue;
		}
		if (xml.compare(i, 2, "</") == 0)
		{
			const std::size_t end = xml.find('>', i + 2);
			if (end == std::string_view::npos)
				return fail("end tag not closed");
			std::string_view name = xml.substr(i + 2, end - i - 2);
			while (!name.empty() && isBlank(name.back()))
				name.remove_suffix(1);
			if (stack.empty() || stack.back()->name != name)
				return fail("end tag </" + std::string(name) + "> does not match");
			stack.pop_back();
			i = end + 1;
			continue;
		}

		// start tag
		++i;
		std::size_t n = i;
		while (n < xml.size() && isNameChar(xml[n]))
			++n;
		XmlNode node;
		node.name = std::string(xml.substr(i, n - i));
		if (node.name.empty())
			return fail("element without a name");
		i = n;
		bool selfClosing = false;
		for (;;)
		{
			while (i < xml.size() && isBlank(xml[i]))
				++i;
			if (i >= xml.size())
				return fail("start tag not closed");
			if (xml[i] == '>')
			{
				++i;
				break;
			}
			if (xml[i] == '/' && i + 1 < xml.size() && xml[i + 1] == '>')
			{
				selfClosing = true;
				i += 2;
				break;
			}
			std::size_t a = i;
			while (a < xml.size() && isNameChar(xml[a]))
				++a;
			if (a == i)
				return fail("bad attribute in <" + node.name + ">");
			std::string attrName(xml.substr(i, a - i));
			i = a;
			while (i < xml.size() && isBlank(xml[i]))
				++i;
			if (i >= xml.size() || xml[i] != '=')
				return fail("attribute without value in <" + node.name + ">");
			++i;
			while (i < xml.size() && isBlank(xml[i]))
				++i;
			if (i >= xml.size() || (xml[i] != '"' && xml[i] != '\''))
				return fail("attribute value not quoted in <" + node.name + ">");
			const char quote = xml[i++];
			const std::size_t close = xml.find(quote, i);
			if (close == std::string_view::npos)
				return fail("attribute value not closed");
			std::string value;
			if (!decodeEntities(xml.substr(i, close - i), value))
				return fail("bad entity in attribute");
			node.attributes.emplace_back(std::move(attrName), std::move(value));
			i = close + 1;
		}

		XmlNode *placed = nullptr;
		if (stack.empty())
		{
			if (haveRoot)
				return fail("more than one root element");
			root = std::move(node);
			haveRoot = true;
			placed = &root;
		}
		else
		{
			stack.back()->children.push_back(std::move(node));
			placed = &stack.back()->children.back();
		}
		if (!selfClosing)
		{
			if (stack.size() >= MAX_DEPTH)
				return fail("elements nested too deeply");
			stack.push_back(placed);
		}
	}
	if (!haveRoot)
		return fail("no root element");
	if (!stack.empty())
		return fail("element <" + stack.back()->name + "> not closed");
	return true;
}

std::string xmlEscape(std::string_view text)
{
	std::string out;
	out.reserve(text.size());
	for (const char c : text)
	{
		const auto u = static_cast<unsigned char>(c);
		if (u < 0x20 && c != '\t' && c != '\n' && c != '\r')
			continue;
		switch (c)
		{
		case '&':
			out += "&amp;";
			break;
		case '<':
			out += "&lt;";
			break;
		case '>':
			out += "&gt;";
			break;
		case '"':
			out += "&quot;";
			break;
		case '\r':
			out += "&#13;";
			break;
		default:
			out.push_back(c);
		}
	}
	return out;
}

} // namespace Babylon
