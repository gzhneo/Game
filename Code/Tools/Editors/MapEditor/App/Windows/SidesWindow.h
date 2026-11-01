/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SidesWindow.h -- the players and teams of the map: a page per player (name, faction, colour,
// allies and enemies) and per team (identity, reinforcements, behaviour, units). Changes go to the map
// through the EditorContext as steps of the history.

#pragma once

#include "Model/Map.h"

#include <QDialog>
#include <QString>

namespace MapEditorApp
{

class EditorContext;

// A player's name for the editor's lists ("(neutral)" for the neutral player) and its display name.
[[nodiscard]] QString playerLabel(const MapModel::Side &side);
[[nodiscard]] QString displayName(const MapModel::Side &side);

class SidesWindow : public QDialog
{
	Q_OBJECT

public:
	SidesWindow(EditorContext &editor, QWidget *parent);

	// Builds the window anew from the map.
	void refresh();
	// The page shown: 0 players, 1 teams; the player and the team selected on them.
	void setTab(int tab) { m_tab = tab; }
	void setPlayer(int player) { m_player = player; }
	void setTeam(int team) { m_team = team; }
	[[nodiscard]] int tab() const { return m_tab; }
	[[nodiscard]] int player() const { return m_player; }
	[[nodiscard]] int team() const { return m_team; }

private:
	EditorContext &m_editor;
	int m_tab = 0;
	int m_player = 0;
	int m_team = -1;
	int m_teamTab = 0;
};

} // namespace MapEditorApp
