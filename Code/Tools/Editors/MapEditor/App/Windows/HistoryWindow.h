/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HistoryWindow.h -- the undo history of the open map as a list: row 0 is the map as opened, row n the
// state after step n; undone steps are grey, the current state bold, the saved one marked. A click asks
// to go to that state.

#pragma once

#include "Core/History.h"

#include <QAbstractListModel>
#include <QDialog>

#include <functional>
#include <string>

class QListView;

namespace MapEditorApp
{

class HistoryModel : public QAbstractListModel
{
	Q_OBJECT

public:
	using StepName = std::function<QString(const std::string &)>;

	HistoryModel(StepName stepName, QObject *parent) : QAbstractListModel(parent), m_stepName(std::move(stepName)) {}

	// Shows the history (null: none); call again after every change.
	void setHistory(const MapEditorCore::History *history);
	[[nodiscard]] int currentRow() const;

	[[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	[[nodiscard]] QVariant data(const QModelIndex &index, int role) const override;

private:
	StepName m_stepName;
	const MapEditorCore::History *m_history = nullptr;
	std::vector<std::string> m_names;
};

class HistoryWindow : public QDialog
{
	Q_OBJECT

public:
	HistoryWindow(HistoryModel::StepName stepName, QWidget *parent);

	void setHistory(const MapEditorCore::History *history);
	[[nodiscard]] HistoryModel *model() const { return m_model; }
	[[nodiscard]] QListView *view() const { return m_view; }

signals:
	// The state after step position (0: as opened) was clicked.
	void goTo(int position);

private:
	HistoryModel *m_model;
	QListView *m_view;
};

} // namespace MapEditorApp
