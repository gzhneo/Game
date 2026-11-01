/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// ScriptsWindow.h -- the players' scripts: the tree of players, groups and scripts, the selected
// script with its conditions and actions, the dialog for one condition or action, the script check,
// and export and import of .scb files. Changes go to the map through the EditorContext as steps of
// the history.

#pragma once

#include "Core/ScriptCommands.h"
#include "Core/ScriptExchange.h"

#include <QDialog>

#include <functional>

namespace MapEditorApp
{

class EditorContext;
class ScriptCatalog;

class ScriptsWindow : public QDialog
{
	Q_OBJECT

public:
	ScriptsWindow(EditorContext &editor, ScriptCatalog &catalog, QWidget *parent);

	// The script, group or player shown (side -1: none).
	[[nodiscard]] const MapEditorCore::ScriptPath &path() const { return m_path; }
	void setPath(const MapEditorCore::ScriptPath &path) { m_path = path; }
	// Builds the window anew from the map.
	void refresh();

	// Edits a condition or action in a dialog; done gets the result when it is confirmed.
	void editCall(bool action, MapModel::ScriptCall call, std::function<void(const MapModel::ScriptCall &)> done);
	// The field for one parameter; changed gets every new value.
	QWidget *parameterField(const MapModel::ScriptParameter &p, const std::function<void(const MapModel::ScriptParameter &)> &changed);
	void showProblems();
	bool exportFile(const QString &path, const MapEditorCore::ScriptExport &options, QString *error);
	void exportDialog();
	// Adds a .scb file to the map as one step; summary and messages say what came in.
	bool importFile(const QString &path, QString *summary, QStringList *messages);
	void importDialog();

private:
	[[nodiscard]] const MapModel::Map *currentMap() const;

	EditorContext &m_editor;
	ScriptCatalog &m_catalog;
	MapEditorCore::ScriptPath m_path;
};

} // namespace MapEditorApp
