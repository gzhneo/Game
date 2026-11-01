/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Document.h -- a layout being edited, with undo and redo.
//
// Every step keeps the text of the layout before and after it. That is enough because the layout model
// writes an unchanged layout byte for byte; undo reads the old text back.

#pragma once

#include "Model/WndLayout.h"

#include <functional>
#include <string>
#include <vector>

namespace WndModel
{

class Document
{
public:
	Document();
	explicit Document(std::string text);

	[[nodiscard]] const Layout &layout() const { return m_layout; }
	[[nodiscard]] std::string text() const { return write(m_layout); }

	// Changes the layout; a change that leaves the text as it was is no step. Returns true if it changed.
	bool edit(const std::string &description, const std::function<void(Layout &)> &change);

	// A change spread over many calls (dragging with the mouse): begin keeps the text, live changes do
	// not count as steps, finish makes one step of all of them (none if the text is back where it began).
	void begin();
	void live(const std::function<void(Layout &)> &change);
	bool finish(const std::string &description);
	void cancel();
	[[nodiscard]] bool inGesture() const { return m_inGesture; }

	[[nodiscard]] bool canUndo() const { return !m_undo.empty(); }
	[[nodiscard]] bool canRedo() const { return !m_redo.empty(); }
	[[nodiscard]] std::string undoText() const { return m_undo.empty() ? std::string() : m_undo.back().description; }
	[[nodiscard]] std::string redoText() const { return m_redo.empty() ? std::string() : m_redo.back().description; }
	bool undo();
	bool redo();
	// The steps that can be undone, oldest first, and those that can be redone, next first.
	[[nodiscard]] std::vector<std::string> undoSteps() const;
	[[nodiscard]] std::vector<std::string> redoSteps() const;

	// Changed since it was read or last saved.
	[[nodiscard]] bool modified() const { return text() != m_saved; }
	void markSaved() { m_saved = text(); }

private:
	struct Step
	{
		std::string description;
		std::string before;
		std::string after;
	};

	void push(const std::string &description, std::string before);

	Layout m_layout;
	std::vector<Step> m_undo;
	std::vector<Step> m_redo;
	std::string m_saved;
	std::string m_gestureStart;
	bool m_inGesture = false;
};

} // namespace WndModel
