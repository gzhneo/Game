/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SearchWindow.h -- search in the whole map: where a name is used (scripts, objects, teams, build
// lists, areas). The hits follow every change of the map; a double-click asks to show the place,
// "Replace all" asks to replace in the scripts.

#pragma once

#include "Core/References.h"
#include "Model/Map.h"

#include <QAbstractTableModel>
#include <QDialog>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QTreeView;

namespace MapEditorApp
{

class ReferenceModel : public QAbstractTableModel
{
	Q_OBJECT

public:
	using QAbstractTableModel::QAbstractTableModel;

	// Searches the map again (null: no map).
	void search(const MapModel::Map *map, const std::string &text, const MapEditorCore::SearchOptions &options);
	[[nodiscard]] const std::vector<MapEditorCore::Reference> &hits() const { return m_hits; }

	[[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	[[nodiscard]] int columnCount(const QModelIndex &parent = QModelIndex()) const override;
	[[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;
	[[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

private:
	const MapModel::Map *m_map = nullptr;
	std::vector<MapEditorCore::Reference> m_hits;
	std::vector<QStringList> m_rows;		// kind, place, value per hit
};

class SearchWindow : public QDialog
{
	Q_OBJECT

public:
	explicit SearchWindow(QWidget *parent);

	// The map searched; call mapChanged after every change of it.
	void setMap(const MapModel::Map *map);
	void mapChanged();
	// Starts a search for the text (whole values or anywhere).
	void search(const QString &text, bool wholeValue);
	[[nodiscard]] const std::vector<MapEditorCore::Reference> &hits() const { return m_model->hits(); }
	[[nodiscard]] QTreeView *view() const { return m_view; }

signals:
	void showReference(const MapEditorCore::Reference &reference);
	void replaceAll(const QString &text, const QString &with, bool wholeValue, bool matchCase);

private:
	void refresh();

	const MapModel::Map *m_map = nullptr;
	ReferenceModel *m_model;
	QLineEdit *m_text;
	QCheckBox *m_whole;
	QCheckBox *m_case;
	QLabel *m_count;
	QTreeView *m_view;
	QLineEdit *m_with;
};

} // namespace MapEditorApp
