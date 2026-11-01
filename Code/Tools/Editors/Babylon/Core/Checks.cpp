/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/Checks.h"

#include "Model/Text.h"

#include <map>
#include <set>
#include <unordered_map>

namespace Babylon
{

namespace
{

std::string lowerAscii(const std::u16string &s)
{
	std::u16string l = s;
	for (char16_t &c : l)
		if (c >= u'A' && c <= u'Z')
			c = static_cast<char16_t>(c - u'A' + u'a');
	return toUtf8(l);
}

} // namespace

std::vector<Finding> strErrors(const Database &str, const std::vector<StrProblem> &problems)
{
	std::vector<Finding> out;
	for (const StrProblem &p : problems)
		if (p.error)
			out.push_back({true, p.line, p.message});

	std::unordered_map<std::u16string, int> labelLines;
	for (const Label &label : str.labels)
	{
		const std::string name = toUtf8(label.name);
		if (!str.allowMultiText && label.texts.size() > 1)
			out.push_back({true, label.line, "label \"" + name + "\" has more than one text"});
		if (const auto it = labelLines.find(label.name); it != labelLines.end())
			out.push_back({true, label.line, "label \"" + name + "\" is already defined on line " + std::to_string(it->second)});
		else
			labelLines.emplace(label.name, label.line);

		std::set<std::u16string> texts;
		for (const Text &text : label.texts)
		{
			if (!texts.insert(text.text).second)
				out.push_back({true, text.line, "label \"" + name + "\" has the same text twice"});
			if (label.maxLen && static_cast<int>(text.text.size()) > label.maxLen)
				out.push_back({true, text.line,
					"the text of label \"" + name + "\" is " + std::to_string(text.text.size()) + " characters long, MAXLEN is " + std::to_string(label.maxLen)});
		}
	}
	return out;
}

std::vector<Finding> strWarnings(const Database &str, const std::vector<StrProblem> &problems)
{
	std::vector<Finding> out;
	for (const StrProblem &p : problems)
		if (!p.error)
			out.push_back({false, p.line, p.message});

	std::unordered_map<std::u16string, int> textLines;
	std::map<std::string, std::pair<std::u16string, int>> lowerLabels;
	for (const Label &label : str.labels)
	{
		const std::string lower = lowerAscii(label.name);
		if (const auto it = lowerLabels.find(lower); it != lowerLabels.end())
		{
			if (it->second.first != label.name)
				out.push_back({false, label.line,
					"label \"" + toUtf8(label.name) + "\" differs only in case from \"" + toUtf8(it->second.first) + "\" on line " + std::to_string(it->second.second)});
		}
		else
			lowerLabels.emplace(lower, std::make_pair(label.name, label.line));

		for (const Text &text : label.texts)
		{
			if (text.text.empty())
			{
				out.push_back({false, text.line, "empty text in label \"" + toUtf8(label.name) + "\""});
				continue;
			}
			if (const auto it = textLines.find(text.text); it != textLines.end())
			{
				if (!str.allowDuplicateText)
					out.push_back({false, text.line, "text is a duplicate of the text on line " + std::to_string(it->second)});
			}
			else
				textLines.emplace(text.text, text.line);
		}
	}
	return out;
}

std::string findingText(const Finding &finding)
{
	std::string s = finding.error ? "Error" : "Warning";
	if (finding.line > 0)
		s += " (line " + std::to_string(finding.line) + ")";
	return s + ": " + finding.message;
}

} // namespace Babylon
