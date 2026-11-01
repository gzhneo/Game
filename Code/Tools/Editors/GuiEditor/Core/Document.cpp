/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Document.cpp -- a layout being edited, with undo and redo.

#include "Core/Document.h"

#include "Core/WndEdit.h"

namespace WndModel
{

namespace
{

constexpr std::size_t MAX_STEPS = 500;

} // namespace

Document::Document() : m_layout(newLayout()), m_saved(write(m_layout)) {}

Document::Document(std::string text) : m_layout(read(text)), m_saved(std::move(text)) {}

void Document::push(const std::string &description, std::string before)
{
	std::string after = text();
	if (after == before)
		return;
	m_undo.push_back({description, std::move(before), std::move(after)});
	if (m_undo.size() > MAX_STEPS)
		m_undo.erase(m_undo.begin());
	m_redo.clear();
}

bool Document::edit(const std::string &description, const std::function<void(Layout &)> &change)
{
	if (m_inGesture)
		cancel();
	std::string before = text();
	change(m_layout);
	const bool changed = text() != before;
	push(description, std::move(before));
	return changed;
}

void Document::begin()
{
	m_gestureStart = text();
	m_inGesture = true;
}

void Document::live(const std::function<void(Layout &)> &change)
{
	if (!m_inGesture)
		begin();
	// Every live change starts from the beginning of the gesture, so rounding never adds up.
	m_layout = read(m_gestureStart);
	change(m_layout);
}

bool Document::finish(const std::string &description)
{
	if (!m_inGesture)
		return false;
	m_inGesture = false;
	const bool changed = text() != m_gestureStart;
	push(description, std::move(m_gestureStart));
	m_gestureStart.clear();
	return changed;
}

void Document::cancel()
{
	if (!m_inGesture)
		return;
	m_layout = read(m_gestureStart);
	m_gestureStart.clear();
	m_inGesture = false;
}

std::vector<std::string> Document::undoSteps() const
{
	std::vector<std::string> out;
	for (const Step &s : m_undo)
		out.push_back(s.description);
	return out;
}

std::vector<std::string> Document::redoSteps() const
{
	std::vector<std::string> out;
	for (auto it = m_redo.rbegin(); it != m_redo.rend(); ++it)
		out.push_back(it->description);
	return out;
}

bool Document::undo()
{
	if (m_inGesture)
		cancel();
	if (m_undo.empty())
		return false;
	Step step = std::move(m_undo.back());
	m_undo.pop_back();
	m_layout = read(step.before);
	m_redo.push_back(std::move(step));
	return true;
}

bool Document::redo()
{
	if (m_inGesture)
		cancel();
	if (m_redo.empty())
		return false;
	Step step = std::move(m_redo.back());
	m_redo.pop_back();
	m_layout = read(step.after);
	m_undo.push_back(std::move(step));
	return true;
}

} // namespace WndModel
