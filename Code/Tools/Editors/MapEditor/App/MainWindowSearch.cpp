/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowSearch.cpp -- the search window (Windows/SearchWindow) on the open map: a jump to each place
// and replace in the scripts as one undo step. "Where is this used?" starts the search with the name of
// the selected object.

#include "App/MainWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"
#include "App/Windows/SearchWindow.h"

namespace MapEditorApp
{

void MainWindow::showReference(const MapEditorCore::Reference &r)
{
	using Kind = MapEditorCore::Reference::Kind;
	if (!m_state.map)
		return;
	const MapModel::Map &map = m_state.map->map();
	switch (r.kind)
	{
		case Kind::ScriptName:
		case Kind::ScriptComment:
		case Kind::ScriptParameter:
			showScripts(&r.path);
			break;
		case Kind::Object:
			if (map.objects && r.index < static_cast<int>(map.objects->size()))
			{
				m_state.map->selection.set(r.index);
				selectionChanged();
				centerOnSelection();
			}
			break;
		case Kind::Team:
		case Kind::BuildList:
			showSides(r.kind == Kind::Team ? 1 : 0);
			break;
		case Kind::Area:
			if (map.polygons && r.index < static_cast<int>(map.polygons->size()) && !(*map.polygons)[static_cast<std::size_t>(r.index)].points.empty())
			{
				const auto &p = (*map.polygons)[static_cast<std::size_t>(r.index)].points.front();
				centerOn(static_cast<float>(p[0]), static_cast<float>(p[1]));
			}
			break;
	}
}

void MainWindow::showSearch(const QString &text, bool wholeValue)
{
	if (!m_state.map)
		return;
	if (!m_searchWindow)
	{
		m_searchWindow = new SearchWindow(this);
		connect(m_searchWindow, &SearchWindow::showReference, this, [this](const MapEditorCore::Reference &r) { showReference(r); });
		connect(m_searchWindow, &SearchWindow::replaceAll, this,
			[this](const QString &find, const QString &with, bool whole, bool matchCase) { replaceInScripts(find, with, whole, matchCase); });
	}
	m_searchWindow->setMap(&m_state.map->map());
	if (!text.isEmpty())
		m_searchWindow->search(text, wholeValue);
	m_searchWindow->show();
	m_searchWindow->raise();
}

int MainWindow::replaceInScripts(const QString &text, const QString &with, bool wholeValue, bool matchCase)
{
	if (!m_state.map || !m_state.map->map().sides || text.isEmpty())
		return 0;
	MapEditorCore::SearchOptions options;
	options.wholeValue = wholeValue;
	options.matchCase = matchCase;
	int count = 0;
	editScripts("Replace in scripts", [&](MapModel::Sides &sides) {
		count = MapEditorCore::replaceInScripts(sides, text.toStdString(), with.toStdString(), options);
		return count > 0;
	});
	toast(count > 0 ? T("%1 values replaced in the scripts.").arg(count) : T("The scripts do not contain the text."));
	return count;
}

// The name the selected object is known by in scripts and teams: its own name, the waypoint's name,
// else its type.
QString MainWindow::selectionName() const
{
	if (!m_state.map || m_state.map->selection.objects.empty() || !m_state.map->map().objects)
		return {};
	const std::int32_t index = m_state.map->selection.objects.front();
	const std::vector<MapModel::MapObject> &objects = *m_state.map->map().objects;
	if (index < 0 || index >= static_cast<std::int32_t>(objects.size()))
		return {};
	const MapModel::MapObject &o = objects[static_cast<std::size_t>(index)];
	for (const char *key : {"objectName", "waypointName"})
	{
		const std::optional<std::string> name = o.properties.getAscii(key);
		if (name && !name->empty())
			return utf8(*name);
	}
	return utf8(o.templateName);
}

void MainWindow::whereUsed()
{
	const QString name = selectionName();
	if (name.isEmpty())
	{
		showSearch();
		return;
	}
	showSearch(name, true);
}

} // namespace MapEditorApp
