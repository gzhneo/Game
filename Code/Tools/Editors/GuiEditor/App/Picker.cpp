/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Picker.cpp -- a list to choose a value from.

#include "App/Picker.h"

#include "EditorCommon/Language.h"

#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace GuiEditorApp
{

using EditorCommon::T;

namespace
{

constexpr int ICONS_PER_STEP = 24;

} // namespace

Picker::Picker(QWidget *parent, const QString &title, std::vector<Item> items, const QString &current,
	std::function<QIcon(const QString &)> icon, int iconSize, bool free)
	: QDialog(parent), m_items(std::move(items)), m_icon(std::move(icon)), m_free(free)
{
	setObjectName(QStringLiteral("Picker"));
	setWindowTitle(title);
	resize(520, 600);
	auto *box = new QVBoxLayout(this);
	m_search = new QLineEdit;
	m_search->setObjectName(QStringLiteral("PickerSearch"));
	m_search->setPlaceholderText(T("Search"));
	m_search->setClearButtonEnabled(true);
	m_list = new QListWidget;
	m_list->setObjectName(QStringLiteral("PickerList"));
	m_list->setUniformItemSizes(true);
	if (iconSize > 0)
		m_list->setIconSize(QSize(iconSize, iconSize));
	for (const Item &item : m_items)
	{
		auto *row = new QListWidgetItem(item.detail.isEmpty() ? item.value : item.value + QStringLiteral("  \u2013  ") + item.detail, m_list);
		row->setData(Qt::UserRole, item.value);
		row->setToolTip(item.detail);
		if (item.value == current)
			m_list->setCurrentItem(row);
	}
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	box->addWidget(m_search);
	box->addWidget(m_list, 1);
	box->addWidget(buttons);
	connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	connect(m_list, &QListWidget::itemActivated, this, [this] { accept(); });
	connect(m_search, &QLineEdit::textChanged, this, [this](const QString &text) { filter(text); });
	connect(m_search, &QLineEdit::returnPressed, this, [this] { accept(); });
	if (m_list->currentItem())
		m_list->scrollToItem(m_list->currentItem(), QAbstractItemView::PositionAtCenter);
	if (m_icon)
	{
		m_iconTimer = new QTimer(this);
		m_iconTimer->setInterval(0);
		connect(m_iconTimer, &QTimer::timeout, this, [this] { loadIcons(); });
		m_iconTimer->start();
	}
	m_search->setFocus();
}

void Picker::filter(const QString &text)
{
	QListWidgetItem *first = nullptr;
	for (int i = 0; i < m_list->count(); ++i)
	{
		QListWidgetItem *row = m_list->item(i);
		const bool shown = text.isEmpty() || row->text().contains(text, Qt::CaseInsensitive);
		row->setHidden(!shown);
		if (shown && !first)
			first = row;
	}
	if (first && (!m_list->currentItem() || m_list->currentItem()->isHidden()))
		m_list->setCurrentItem(first);
}

void Picker::loadIcons()
{
	// Pictures load in small steps so the list can be used while they come.
	for (int n = 0; n < ICONS_PER_STEP && m_nextIcon < m_list->count(); ++n, ++m_nextIcon)
	{
		QListWidgetItem *row = m_list->item(m_nextIcon);
		row->setIcon(m_icon(row->data(Qt::UserRole).toString()));
	}
	if (m_nextIcon >= m_list->count())
		m_iconTimer->stop();
}

QString Picker::chosen() const
{
	QListWidgetItem *row = m_list->currentItem();
	if (row && !row->isHidden())
		return row->data(Qt::UserRole).toString();
	return m_free ? m_search->text().trimmed() : QString();
}

bool Picker::pick(QWidget *parent, const QString &title, std::vector<Item> items, const QString &current, QString *value,
	std::function<QIcon(const QString &)> icon, int iconSize, bool free)
{
	Picker dialog(parent, title, std::move(items), current, std::move(icon), iconSize, free);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	const QString chosen = dialog.chosen();
	if (chosen.isEmpty())
		return false;
	*value = chosen;
	return true;
}

} // namespace GuiEditorApp
