/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SidesCommands.h -- players and teams as the old WorldBuilder's player list and teams dialog edit
// them (PlayerListDlg, CTeamsDialog and its pages), kept valid the way SidesList::validateSides
// does it: one neutral player, a default team "team<player>" per player, allies and enemies that
// are other players, team owners that exist.

#pragma once

#include "Core/History.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace MapEditorCore
{

// MAX_PLAYER_COUNT of the game; the old list kept one place free for an observer.
constexpr int MAX_PLAYERS = 16;

[[nodiscard]] std::string playerName(const MapModel::Side &side);
[[nodiscard]] std::string teamName(const MapModel::PropertyMap &team);
[[nodiscard]] std::string teamOwner(const MapModel::PropertyMap &team);
[[nodiscard]] int findPlayer(const MapModel::Sides &sides, const std::string &name);
[[nodiscard]] int findTeam(const MapModel::Sides &sides, const std::string &name);
// "team<player>" of an existing player (SidesList::isPlayerDefaultTeam).
[[nodiscard]] bool isDefaultTeam(const MapModel::Sides &sides, const MapModel::PropertyMap &team);
// Letters, digits and '_' only; anything else becomes '_'.
[[nodiscard]] std::string validPlayerName(const std::string &name);
// Space-separated names (playerAllies, playerEnemies).
[[nodiscard]] std::vector<std::string> nameList(const std::string &names);

// SidesList::validateSides; true if it had to change anything.
bool validateSides(MapModel::Sides &sides);

enum class Relation
{
	Neutral,
	Ally,
	Enemy
};
// How player from regards player to (its allies and enemies lists).
[[nodiscard]] Relation relation(const MapModel::Sides &sides, int from, int to);
void setRelation(MapModel::Sides &sides, int from, int to, Relation relation);

// The first free "player0001", "player0002", ...
[[nodiscard]] std::string freePlayerName(const MapModel::Sides &sides);
// Adds a player with its default team; returns its index, -1 if the list is full or the name is taken.
int addPlayer(MapModel::Sides &sides, const std::string &name, const std::u16string &displayName, const std::string &faction, bool human);
// Removes a player (never the neutral one) and every team it owns.
bool removePlayer(MapModel::Sides &sides, int index);
// Renames a player: teams it owns, its default team and the objects of that team follow.
bool renamePlayer(MapModel::Map &map, int index, const std::string &name);
// The skirmish AI players (civilian and each faction; generals too in Zero Hour) that are missing;
// returns how many were added.
int addSkirmishPlayers(MapModel::Sides &sides, bool zeroHour);

// Adds a team "team0001", ... owned by the player; returns its index.
int addTeam(MapModel::Sides &sides, const std::string &owner);
// A copy named "<name>.01", ... after the original; returns its index, -1 for a default team.
int copyTeam(MapModel::Sides &sides, int index);
// Removes a team that is not a default team.
bool removeTeam(MapModel::Sides &sides, int index);
// Swaps a team with its neighbour (delta -1 or 1); default teams stay where they are.
bool moveTeam(MapModel::Sides &sides, int index, int delta);
// Renames a team; the objects of the team follow. False if the name is empty or taken.
bool renameTeam(MapModel::Map &map, int index, const std::string &name);
// Objects whose team (originalOwner) is the one named.
[[nodiscard]] std::vector<std::int32_t> teamObjects(const MapModel::Map &map, const std::string &team);
// Objects of every team the player owns.
[[nodiscard]] int playerObjectCount(const MapModel::Map &map, int index);

// Players and teams (and the objects, if a rename moved them) before and after, as one step. Steps
// of the same name that follow each other fold together if marked mergeable (typing in a field).
class EditSides : public Command
{
public:
	struct State
	{
		std::optional<MapModel::Sides> sides;
		std::optional<std::vector<MapModel::MapObject>> objects;
	};

	EditSides(std::string name, State before, State after, bool mergeable)
		: m_name(std::move(name)), m_before(std::move(before)), m_after(std::move(after)), m_mergeable(mergeable) {}

	virtual void apply(MapModel::Map &map) override { set(map, m_after); }
	virtual void revert(MapModel::Map &map) override { set(map, m_before); }
	virtual std::string name() const override { return m_name; }
	virtual Changes changes() const override;
	virtual bool mergeWith(const Command &next) override;

private:
	static void set(MapModel::Map &map, const State &state);

	std::string m_name;
	State m_before;
	State m_after;
	bool m_mergeable;
};

EditSides::State sidesState(const MapModel::Map &map);
// The step from the state before to the map now; null if nothing changed. The objects are kept only
// if they changed.
std::unique_ptr<EditSides> sidesStep(const MapModel::Map &map, EditSides::State before, std::string name, bool mergeable = false);

} // namespace MapEditorCore
