/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/HistoryWindow.h"

#include "App/Language.h"
#include "App/Widgets.h"

#include <QApplication>
#include <QFont>
#include <QListView>
#include <QPalette>
#include <QVBoxLayout>

namespace MapEditorApp
{

void HistoryModel::setHistory(const MapEditorCore::History *history)
{
	beginResetModel();
	m_history = history;
	m_names = history ? history->names() : std::vector<std::string>();
	endResetModel();
}

int HistoryModel::currentRow() const
{
	return m_history ? static_cast<int>(m_history->undoCount()) : -1;
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
	return parent.isValid() || !m_history ? 0 : static_cast<int>(m_names.size()) + 1;
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
	if (!m_history || !index.isValid())
		return {};
	const std::size_t row = static_cast<std::size_t>(index.row());
	const std::size_t done = m_history->undoCount();
	switch (role)
	{
		case Qt::DisplayRole:
		{
			QString text = row == 0 ? T("Map opened") : QStringLiteral("%1. %2").arg(row).arg(m_stepName(m_names[row - 1]));
			if (row == m_history->savedPosition())
				text += QStringLiteral("  ") + T("(saved)");
			return text;
		}
		case Qt::ForegroundRole:
			if (row > done)
				return QApplication::palette().color(QPalette::Disabled, QPalette::Text);
			return {};
		case Qt::FontRole:
			if (row == done)
			{
				QFont bold = QApplication::font();
				bold.setBold(true);
				return bold;
			}
			return {};
		default:
			return {};
	}
}

HistoryWindow::HistoryWindow(HistoryModel::StepName stepName, QWidget *parent)
	: QDialog(parent), m_model(new HistoryModel(std::move(stepName), this)), m_view(new QListView(this))
{
	setObjectName(QStringLiteral("HistoryWindow"));
	setWindowTitle(T("History"));
	resize(340, 420);
	auto *col = new QVBoxLayout(this);
	m_view->setObjectName(QStringLiteral("HistoryList"));
	m_view->setModel(m_model);
	m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
	col->addWidget(m_view, 1);
	connect(m_view, &QListView::clicked, this, [this](const QModelIndex &index) { emit goTo(index.row()); });
	auto *hint = label(T("Click a step to go back or forward to it. Greyed steps are undone; the next change drops them."), "Hint", this);
	hint->setWordWrap(true);
	col->addWidget(hint);
}

void HistoryWindow::setHistory(const MapEditorCore::History *history)
{
	m_model->setHistory(history);
	const QModelIndex current = m_model->index(m_model->currentRow());
	m_view->setCurrentIndex(current);
	m_view->scrollTo(current);
}

} // namespace MapEditorApp
