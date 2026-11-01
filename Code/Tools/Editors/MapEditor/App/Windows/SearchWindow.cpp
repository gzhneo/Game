/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/SearchWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"
#include "Core/ScriptCommands.h"
#include "Core/SidesCommands.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace MapEditorApp
{

namespace
{

// Kind and place of a hit, in the words of the editor's windows.
QStringList describe(const MapModel::Map &map, const MapEditorCore::Reference &r)
{
	using Kind = MapEditorCore::Reference::Kind;
	using Part = MapEditorCore::Reference::Part;
	QString kind;
	QString place;
	if (r.kind == Kind::ScriptName || r.kind == Kind::ScriptComment || r.kind == Kind::ScriptParameter)
	{
		const MapModel::Sides &sides = *map.sides;
		QStringList path;
		if (r.path.side >= 0 && r.path.side < static_cast<int>(sides.sides.size()))
			path << utf8(MapEditorCore::playerName(sides.sides[static_cast<std::size_t>(r.path.side)]));
		if (const MapModel::ScriptGroup *group = r.path.group >= 0 ? MapEditorCore::groupAt(sides, r.path) : nullptr)
			path << utf8(group->name);
		if (const MapModel::Script *script = r.path.script >= 0 ? MapEditorCore::scriptAt(sides, r.path) : nullptr)
			path << utf8(script->name);
		place = path.join(QStringLiteral(" \u203a "));
		if (r.kind == Kind::ScriptName)
			kind = r.path.script >= 0 ? T("Script name") : T("Script group");
		else if (r.kind == Kind::ScriptComment)
			kind = T("Script comment");
		else if (r.part == Part::Condition)
			kind = T("Condition %1, parameter %2").arg(r.call + 1).arg(r.parameter + 1);
		else if (r.part == Part::Action)
			kind = T("Action %1, parameter %2").arg(r.call + 1).arg(r.parameter + 1);
		else
			kind = T("Else action %1, parameter %2").arg(r.call + 1).arg(r.parameter + 1);
	}
	else if (r.kind == Kind::Object)
	{
		kind = r.key.empty() ? T("Object type") : T("Object property %1").arg(utf8(r.key));
		const MapModel::MapObject &o = (*map.objects)[static_cast<std::size_t>(r.index)];
		place = T("%1 at %2, %3").arg(utf8(o.templateName)).arg(o.x, 0, 'f', 0).arg(o.y, 0, 'f', 0);
	}
	else if (r.kind == Kind::Team)
	{
		kind = T("Team property %1").arg(utf8(r.key));
		place = utf8(MapEditorCore::teamName(map.sides->teams[static_cast<std::size_t>(r.index)]));
	}
	else if (r.kind == Kind::BuildList)
	{
		kind = T("Build list");
		place = utf8(MapEditorCore::playerName(map.sides->sides[static_cast<std::size_t>(r.index)]));
	}
	else
	{
		kind = T("Area");
		place = utf8((*map.polygons)[static_cast<std::size_t>(r.index)].name);
	}
	return {kind, place, utf8(r.value)};
}

} // namespace

void ReferenceModel::search(const MapModel::Map *map, const std::string &text, const MapEditorCore::SearchOptions &options)
{
	beginResetModel();
	m_map = map;
	m_hits = map ? MapEditorCore::findReferences(*map, text, options) : std::vector<MapEditorCore::Reference>();
	m_rows.clear();
	for (const MapEditorCore::Reference &r : m_hits)
		m_rows.push_back(describe(*map, r));
	endResetModel();
}

int ReferenceModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int ReferenceModel::columnCount(const QModelIndex &parent) const
{
	return parent.isValid() ? 0 : 3;
}

QVariant ReferenceModel::data(const QModelIndex &index, int role) const
{
	if (role != Qt::DisplayRole || !index.isValid() || index.row() >= static_cast<int>(m_rows.size()))
		return {};
	return m_rows[static_cast<std::size_t>(index.row())].value(index.column());
}

QVariant ReferenceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};
	switch (section)
	{
		case 0: return T("Where");
		case 1: return T("Place");
		case 2: return T("Value");
		default: return {};
	}
}

SearchWindow::SearchWindow(QWidget *parent)
	: QDialog(parent), m_model(new ReferenceModel(this)), m_text(new QLineEdit(this)), m_whole(new QCheckBox(T("Whole value"), this)),
	  m_case(new QCheckBox(T("Match case"), this)), m_count(label(QString(), "Hint", this)), m_view(new QTreeView(this)), m_with(new QLineEdit(this))
{
	setObjectName(QStringLiteral("SearchWindow"));
	setWindowTitle(T("Find in the map"));
	resize(820, 520);
	auto *col = new QVBoxLayout(this);
	auto *row = new QHBoxLayout;
	m_text->setObjectName(QStringLiteral("SearchText"));
	m_text->setPlaceholderText(T("Name, text or template"));
	m_text->setClearButtonEnabled(true);
	row->addWidget(m_text, 1);
	row->addWidget(m_whole);
	row->addWidget(m_case);
	col->addLayout(row);
	col->addWidget(m_count);
	m_view->setObjectName(QStringLiteral("SearchResults"));
	m_view->setModel(m_model);
	m_view->setRootIsDecorated(false);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_view->header()->setSectionResizeMode(1, QHeaderView::Stretch);
	col->addWidget(m_view, 1);
	auto *replaceRow = new QHBoxLayout;
	replaceRow->addWidget(new QLabel(T("Replace in the scripts with:"), this));
	m_with->setObjectName(QStringLiteral("SearchWith"));
	replaceRow->addWidget(m_with, 1);
	auto *replace = new QPushButton(T("Replace all"), this);
	replaceRow->addWidget(replace);
	col->addLayout(replaceRow);
	auto *hint = label(T("A double-click shows the place. Replacing changes script names, comments and text parameters (teams, waypoints, areas, flags, counters ...) in one undo step; objects, teams and areas keep their names."), "Hint", this);
	hint->setWordWrap(true);
	col->addWidget(hint);

	connect(m_text, &QLineEdit::textChanged, this, [this] { refresh(); });
	connect(m_whole, &QCheckBox::toggled, this, [this] { refresh(); });
	connect(m_case, &QCheckBox::toggled, this, [this] { refresh(); });
	connect(m_view, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
		const std::size_t i = static_cast<std::size_t>(index.row());
		if (i < m_model->hits().size())
			emit showReference(m_model->hits()[i]);
	});
	connect(replace, &QPushButton::clicked, this, [this] { emit replaceAll(m_text->text(), m_with->text(), m_whole->isChecked(), m_case->isChecked()); });
}

void SearchWindow::setMap(const MapModel::Map *map)
{
	m_map = map;
	refresh();
}

void SearchWindow::mapChanged()
{
	if (isVisible())
		refresh();
}

void SearchWindow::search(const QString &text, bool wholeValue)
{
	const QSignalBlocker blockWhole(m_whole);
	const QSignalBlocker blockText(m_text);
	m_whole->setChecked(wholeValue);
	m_text->setText(text);
	refresh();
	m_text->setFocus();
}

void SearchWindow::refresh()
{
	MapEditorCore::SearchOptions options;
	options.wholeValue = m_whole->isChecked();
	options.matchCase = m_case->isChecked();
	m_model->search(m_map, m_text->text().toStdString(), options);
	m_view->resizeColumnToContents(0);
	m_count->setText(m_text->text().isEmpty() ? QString() : T("%1 places").arg(m_model->hits().size()));
}

} // namespace MapEditorApp
