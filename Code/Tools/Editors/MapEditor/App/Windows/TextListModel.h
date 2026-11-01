/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// TextListModel.h -- a list of texts for a list view that is filled anew as a whole (the problems of the
// map): the view keeps its place by row, and row n stays item n of the caller's own list.

#pragma once

#include <QAbstractListModel>
#include <QStringList>

namespace MapEditorApp
{

class TextListModel : public QAbstractListModel
{
	Q_OBJECT

public:
	using QAbstractListModel::QAbstractListModel;

	void setTexts(QStringList texts)
	{
		beginResetModel();
		m_texts = std::move(texts);
		endResetModel();
	}

	[[nodiscard]] int rowCount(const QModelIndex &parent = QModelIndex()) const override
	{
		return parent.isValid() ? 0 : static_cast<int>(m_texts.size());
	}
	[[nodiscard]] QVariant data(const QModelIndex &index, int role) const override
	{
		return role == Qt::DisplayRole && index.isValid() && index.row() < m_texts.size() ? QVariant(m_texts[index.row()]) : QVariant();
	}

private:
	QStringList m_texts;
};

} // namespace MapEditorApp
