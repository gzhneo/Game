/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "Core/References.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <variant>

namespace MapEditorCore
{

namespace
{

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

// Position of text in value under the options, npos if none; whole values match at 0 only.
std::size_t findText(const std::string &value, const std::string &text, const SearchOptions &options, std::size_t from = 0)
{
	if (text.empty())
		return std::string::npos;
	const std::string v = options.matchCase ? value : lower(value);
	const std::string t = options.matchCase ? text : lower(text);
	if (options.wholeValue)
		return from == 0 && v == t ? 0 : std::string::npos;
	return v.find(t, from);
}

bool replaceText(std::string &value, const std::string &text, const std::string &with, const SearchOptions &options)
{
	bool changed = false;
	std::size_t at = 0;
	while ((at = findText(value, text, options, at)) != std::string::npos)
	{
		value.replace(at, text.size(), with);
		at += with.size();
		changed = true;
		if (options.wholeValue)
			break;
	}
	return changed;
}

bool isText(const MapModel::ScriptParameter &p)
{
	return valueKind(p.type) == ValueKind::Text;
}

// Visits every call of a script with its part and indices.
void forEachCall(const MapModel::Script &s, const std::function<void(Reference::Part, int, int, const MapModel::ScriptCall &)> &visit)
{
	for (std::size_t o = 0; o < s.conditions.size(); ++o)
		for (std::size_t c = 0; c < s.conditions[o].conditions.size(); ++c)
			visit(Reference::Part::Condition, static_cast<int>(o), static_cast<int>(c), s.conditions[o].conditions[c]);
	for (std::size_t a = 0; a < s.actions.size(); ++a)
		visit(Reference::Part::Action, -1, static_cast<int>(a), s.actions[a]);
	for (std::size_t a = 0; a < s.falseActions.size(); ++a)
		visit(Reference::Part::FalseAction, -1, static_cast<int>(a), s.falseActions[a]);
}

void findInProperties(const MapModel::PropertyMap &properties, Reference::Kind kind, int index, const std::string &text,
	const SearchOptions &options, std::vector<Reference> &out)
{
	for (const MapModel::PropertyMap::Pair &pair : properties.pairs())
	{
		const std::string *value = std::get_if<std::string>(&pair.value);
		if (value && textMatches(*value, text, options))
		{
			Reference r;
			r.kind = kind;
			r.index = index;
			r.key = pair.key;
			r.value = *value;
			out.push_back(std::move(r));
		}
	}
}

} // namespace

bool textMatches(const std::string &value, const std::string &text, const SearchOptions &options)
{
	return findText(value, text, options) != std::string::npos;
}

std::vector<Reference> findReferences(const MapModel::Map &map, const std::string &text, const SearchOptions &options)
{
	std::vector<Reference> out;
	if (text.empty())
		return out;
	if (map.sides)
	{
		const MapModel::Sides &sides = *map.sides;
		// Groups by name first, then every script.
		for (std::size_t side = 0; side < sides.sides.size(); ++side)
		{
			const std::vector<MapModel::ScriptGroup> &groups = sides.sides[side].scripts.groups;
			for (std::size_t g = 0; g < groups.size(); ++g)
			{
				if (!textMatches(groups[g].name, text, options))
					continue;
				Reference r;
				r.kind = Reference::Kind::ScriptName;
				r.path = {static_cast<int>(side), static_cast<int>(g), -1};
				r.value = groups[g].name;
				out.push_back(std::move(r));
			}
		}
		forEachScript(sides, [&](const ScriptPath &path, const MapModel::Script &s) {
			if (textMatches(s.name, text, options))
			{
				Reference r;
				r.kind = Reference::Kind::ScriptName;
				r.path = path;
				r.value = s.name;
				out.push_back(std::move(r));
			}
			for (const std::string *comment : {&s.comment, &s.conditionComment, &s.actionComment})
			{
				if (!textMatches(*comment, text, options))
					continue;
				Reference r;
				r.kind = Reference::Kind::ScriptComment;
				r.path = path;
				r.value = *comment;
				out.push_back(std::move(r));
			}
			forEachCall(s, [&](Reference::Part part, int orIndex, int call, const MapModel::ScriptCall &c) {
				for (std::size_t p = 0; p < c.parameters.size(); ++p)
				{
					if (!isText(c.parameters[p]) || !textMatches(c.parameters[p].text, text, options))
						continue;
					Reference r;
					r.kind = Reference::Kind::ScriptParameter;
					r.path = path;
					r.part = part;
					r.orIndex = orIndex;
					r.call = call;
					r.parameter = static_cast<int>(p);
					r.value = c.parameters[p].text;
					out.push_back(std::move(r));
				}
			});
		});
		for (std::size_t t = 0; t < sides.teams.size(); ++t)
			findInProperties(sides.teams[t], Reference::Kind::Team, static_cast<int>(t), text, options, out);
		for (std::size_t side = 0; side < sides.sides.size(); ++side)
		{
			const std::vector<MapModel::BuildListEntry> &list = sides.sides[side].buildList;
			for (std::size_t e = 0; e < list.size(); ++e)
			{
				for (const std::string *value : {&list[e].buildingName, &list[e].templateName, &list[e].script})
				{
					if (!textMatches(*value, text, options))
						continue;
					Reference r;
					r.kind = Reference::Kind::BuildList;
					r.index = static_cast<int>(side);
					r.entry = static_cast<int>(e);
					r.value = *value;
					out.push_back(std::move(r));
					break;
				}
			}
		}
	}
	if (map.objects)
	{
		for (std::size_t i = 0; i < map.objects->size(); ++i)
		{
			const MapModel::MapObject &o = (*map.objects)[i];
			if (textMatches(o.templateName, text, options))
			{
				Reference r;
				r.kind = Reference::Kind::Object;
				r.index = static_cast<int>(i);
				r.value = o.templateName;
				out.push_back(std::move(r));
			}
			findInProperties(o.properties, Reference::Kind::Object, static_cast<int>(i), text, options, out);
		}
	}
	if (map.polygons)
	{
		for (std::size_t i = 0; i < map.polygons->size(); ++i)
		{
			if (!textMatches((*map.polygons)[i].name, text, options))
				continue;
			Reference r;
			r.kind = Reference::Kind::Area;
			r.index = static_cast<int>(i);
			r.value = (*map.polygons)[i].name;
			out.push_back(std::move(r));
		}
	}
	return out;
}

int replaceInScripts(MapModel::Sides &sides, const std::string &text, const std::string &with, const SearchOptions &options)
{
	int count = 0;
	const auto replaceScript = [&](MapModel::Script &s) {
		for (std::string *value : {&s.name, &s.comment, &s.conditionComment, &s.actionComment})
			count += replaceText(*value, text, with, options) ? 1 : 0;
		const auto calls = [&](std::vector<MapModel::ScriptCall> &list) {
			for (MapModel::ScriptCall &c : list)
				for (MapModel::ScriptParameter &p : c.parameters)
					if (isText(p))
						count += replaceText(p.text, text, with, options) ? 1 : 0;
		};
		for (MapModel::OrCondition &o : s.conditions)
			calls(o.conditions);
		calls(s.actions);
		calls(s.falseActions);
	};
	for (MapModel::Side &side : sides.sides)
	{
		for (MapModel::Script &s : side.scripts.scripts)
			replaceScript(s);
		for (MapModel::ScriptGroup &g : side.scripts.groups)
		{
			count += replaceText(g.name, text, with, options) ? 1 : 0;
			for (MapModel::Script &s : g.scripts)
				replaceScript(s);
		}
	}
	return count;
}

} // namespace MapEditorCore
