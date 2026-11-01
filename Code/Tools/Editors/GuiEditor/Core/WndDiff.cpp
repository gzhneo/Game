/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// WndDiff.cpp -- what differs between two window layouts.

#include "Core/WndDiff.h"

#include <map>
#include <set>
#include <utility>

namespace WndModel
{

namespace
{

struct Placed
{
	const Window *window = nullptr;
	std::string parent;		// name of the parent, empty at the top
};

// Name and occurrence of every window, in file order.
using Key = std::pair<std::string, int>;

void collect(const Window &w, const std::string &parent, std::vector<std::pair<Key, Placed>> &out, std::map<std::string, int> &seen)
{
	const std::string name = w.name();
	out.push_back({{name, seen[name]++}, {&w, parent}});
	for (const Window *child : w.children())
		collect(*child, name, out, seen);
}

std::vector<std::pair<Key, Placed>> placedWindows(const Layout &layout)
{
	std::vector<std::pair<Key, Placed>> out;
	std::map<std::string, int> seen;
	for (const Window *w : layout.windows())
		collect(*w, {}, out, seen);
	return out;
}

// The value with every run of whitespace made one space.
std::string normalized(std::string_view s)
{
	std::string out;
	bool space = false;
	for (const char c : s)
	{
		if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
			space = !out.empty();
		else
		{
			if (space)
				out += ' ';
			out += c;
			space = false;
		}
	}
	return out;
}

// The fields of the entries by key and occurrence, values normalized, NAME left out (the match uses it).
std::vector<std::pair<Key, std::string>> fieldsOf(const std::vector<Entry> &entries)
{
	std::vector<std::pair<Key, std::string>> out;
	std::map<std::string, int> seen;
	for (const Entry &e : entries)
		if (e.kind == Entry::Kind::Field && e.key != "NAME")
			out.push_back({{e.key, seen[e.key]++}, normalized(e.value())});
	return out;
}

std::vector<FieldDiff> diffFields(const std::vector<Entry> &a, const std::vector<Entry> &b)
{
	const std::vector<std::pair<Key, std::string>> fa = fieldsOf(a);
	const std::vector<std::pair<Key, std::string>> fb = fieldsOf(b);
	const std::map<Key, std::string> before(fa.begin(), fa.end());
	const std::map<Key, std::string> after(fb.begin(), fb.end());
	std::vector<FieldDiff> out;
	for (const auto &[key, value] : fb)
	{
		const auto it = before.find(key);
		if (it == before.end())
			out.push_back({key.first, {}, value});
		else if (it->second != value)
			out.push_back({key.first, it->second, value});
	}
	for (const auto &[key, value] : fa)
		if (!after.count(key))
			out.push_back({key.first, value, {}});
	return out;
}

} // namespace

std::vector<WindowDiff> diffLayouts(const Layout &a, const Layout &b)
{
	std::vector<WindowDiff> out;
	if (std::vector<FieldDiff> f = diffFields(a.entries, b.entries); !f.empty())
		out.push_back({WindowDiff::Kind::Changed, {}, std::move(f)});

	const std::vector<std::pair<Key, Placed>> wa = placedWindows(a);
	const std::vector<std::pair<Key, Placed>> wb = placedWindows(b);
	const std::map<Key, Placed> inA(wa.begin(), wa.end());
	std::set<Key> inB;
	for (const auto &[key, placed] : wb)
	{
		inB.insert(key);
		const auto it = inA.find(key);
		if (it == inA.end())
		{
			out.push_back({WindowDiff::Kind::Added, key.first, {}});
			continue;
		}
		std::vector<FieldDiff> f = diffFields(it->second.window->entries, placed.window->entries);
		if (it->second.parent != placed.parent)
			f.insert(f.begin(), {"parent", it->second.parent, placed.parent});
		if (!f.empty())
			out.push_back({WindowDiff::Kind::Changed, key.first, std::move(f)});
	}
	for (const auto &[key, placed] : wa)
		if (!inB.count(key))
			out.push_back({WindowDiff::Kind::Removed, key.first, {}});
	return out;
}

} // namespace WndModel
