/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowShortcuts.cpp -- own keyboard shortcuts for the menu commands. A command is known by its
// menu path in the English source texts, so a shortcut stays when the language changes. The choices
// are kept in the settings and applied whenever the menus are built.

#include "App/MainWindow.h"
#include "App/MainWindowParts.h"

#include "App/Language.h"
#include "App/Widgets.h"

#include <QAction>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace MapEditorApp
{

namespace
{

constexpr const char *DEFAULT_PROPERTY = "defaultShortcuts";

QString plain(QString text)
{
	text.remove(QLatin1Char('&'));
	// A shortcut shown after a tab is no part of the name.
	const qsizetype tab = text.indexOf(QLatin1Char('\t'));
	return tab >= 0 ? text.left(tab) : text;
}

} // namespace

void forEachAction(QMenuBar *bar, const std::function<void(QAction *, const QString &shownPath)> &visit)
{
	std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &path) {
		for (QAction *action : menu->actions())
		{
			if (action->isSeparator())
				continue;
			const QString shown = path + QStringLiteral(" \u203a ") + plain(action->text());
			if (action->menu())
				walk(action->menu(), shown);
			else
				visit(action, shown);
		}
	};
	for (QAction *top : bar->actions())
		if (top->menu())
			walk(top->menu(), plain(top->text()));
}

void MainWindow::applyShortcuts(QMenuBar *bar)
{
	std::function<void(QMenu *, const QString &)> walk = [&](QMenu *menu, const QString &id) {
		for (QAction *action : menu->actions())
		{
			if (action->isSeparator())
				continue;
			const QString own = id + QLatin1Char('/') + sourceText(plain(action->text()));
			if (action->menu())
			{
				walk(action->menu(), own);
				continue;
			}
			action->setProperty(ID_PROPERTY, own);
			action->setProperty(DEFAULT_PROPERTY, QKeySequence::listToString(action->shortcuts(), QKeySequence::PortableText));
			const auto chosen = m_state.shortcuts.find(own);
			if (chosen != m_state.shortcuts.end())
				action->setShortcuts(QKeySequence::listFromString(chosen->second, QKeySequence::PortableText));
		}
	};
	for (QAction *top : bar->actions())
		if (top->menu())
			walk(top->menu(), sourceText(plain(top->text())));
}

QString MainWindow::shortcutConflict(const QKeySequence &keys, const QString &exceptId)
{
	QString found;
	QMenuBar *bar = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr;
	if (!bar || keys.isEmpty())
		return found;
	forEachAction(bar, [&](QAction *action, const QString &shown) {
		if (action->property(ID_PROPERTY).toString() == exceptId)
			return;
		for (const QKeySequence &k : action->shortcuts())
			if (k.matches(keys) == QKeySequence::ExactMatch)
				found = shown;
	});
	return found;
}

void MainWindow::showShortcuts()
{
	QMenuBar *bar = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr;
	if (!bar)
		return;
	auto *dialog = new QDialog(this);
	dialog->setObjectName(QStringLiteral("ShortcutsWindow"));
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(T("Keyboard shortcuts"));
	dialog->resize(720, 560);
	auto *col = new QVBoxLayout(dialog);
	auto *hint = label(T("Double-click a command to give it another shortcut. Backspace in the field removes it."), "Hint", dialog);
	hint->setWordWrap(true);
	col->addWidget(hint);
	auto *tree = new QTreeWidget(dialog);
	tree->setObjectName(QStringLiteral("ShortcutList"));
	tree->setColumnCount(2);
	tree->setHeaderLabels({T("Command"), T("Shortcut")});
	tree->setRootIsDecorated(false);
	tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	col->addWidget(tree, 1);
	const auto fill = [this, tree] {
		tree->clear();
		QMenuBar *menus = menuWidget() ? menuWidget()->findChild<QMenuBar *>() : nullptr;
		if (!menus)
			return;
		forEachAction(menus, [tree](QAction *action, const QString &shown) {
			const QString id = action->property(ID_PROPERTY).toString();
			if (id.isEmpty())
				return;
			auto *item = new QTreeWidgetItem(tree, {shown, QKeySequence::listToString(action->shortcuts(), QKeySequence::NativeText)});
			item->setData(0, Qt::UserRole, id);
		});
	};
	fill();
	connect(tree, &QTreeWidget::itemDoubleClicked, this, [this, dialog, fill](QTreeWidgetItem *item) {
		const QString id = item->data(0, Qt::UserRole).toString();
		QDialog ask(dialog);
		ask.setWindowTitle(item->text(0));
		auto *v = new QVBoxLayout(&ask);
		auto *edit = new QKeySequenceEdit(QKeySequence(item->text(1).section(QStringLiteral(", "), 0, 0), QKeySequence::NativeText), &ask);
		edit->setClearButtonEnabled(true);
		v->addWidget(edit);
		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, &ask);
		connect(buttons, &QDialogButtonBox::accepted, &ask, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, &ask, &QDialog::reject);
		bool restore = false;
		connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &ask, [&ask, &restore] {
			restore = true;
			ask.accept();
		});
		v->addWidget(buttons);
		edit->setFocus();
		if (ask.exec() != QDialog::Accepted)
			return;
		if (restore)
			m_state.shortcuts.erase(id);
		else
		{
			const QKeySequence keys = edit->keySequence();
			const QString other = shortcutConflict(keys, id);
			if (!other.isEmpty())
			{
				toast(T("%1 is taken by %2.").arg(keys.toString(QKeySequence::NativeText), other));
				return;
			}
			m_state.shortcuts[id] = keys.toString(QKeySequence::PortableText);
		}
		m_state.save();
		// The menus are built anew with the shortcut; the list follows.
		rebuild();
		fill();
	});
	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
	QPushButton *reset = buttons->addButton(T("Reset all"), QDialogButtonBox::ResetRole);
	connect(reset, &QPushButton::clicked, this, [this, fill] {
		m_state.shortcuts.clear();
		m_state.save();
		rebuild();
		fill();
	});
	connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
	col->addWidget(buttons);
	dialog->show();
}

} // namespace MapEditorApp
