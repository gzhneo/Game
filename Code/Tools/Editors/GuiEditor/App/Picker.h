/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Picker.h -- a list to choose a value from (images, fonts, header templates, callbacks, texts), with a
// search field; pictures load a few at a time so long lists open at once.

#pragma once

#include <QDialog>
#include <QIcon>
#include <QString>

#include <functional>
#include <vector>

class QLineEdit;
class QListWidget;
class QTimer;

namespace GuiEditorApp
{

class Picker : public QDialog
{
public:
	struct Item
	{
		QString value;		// what goes into the file
		QString detail;		// shown behind it (the text of a key, the size of an image)
	};

	// icon: the picture of a value (empty for none); free: the typed text counts when nothing matches.
	Picker(QWidget *parent, const QString &title, std::vector<Item> items, const QString &current,
		std::function<QIcon(const QString &)> icon = {}, int iconSize = 0, bool free = false);

	[[nodiscard]] QString chosen() const;

	// Opens the picker; true and the value if one was chosen.
	static bool pick(QWidget *parent, const QString &title, std::vector<Item> items, const QString &current, QString *value,
		std::function<QIcon(const QString &)> icon = {}, int iconSize = 0, bool free = false);

private:
	void filter(const QString &text);
	void loadIcons();

	std::vector<Item> m_items;
	std::function<QIcon(const QString &)> m_icon;
	bool m_free = false;
	QLineEdit *m_search = nullptr;
	QListWidget *m_list = nullptr;
	QTimer *m_iconTimer = nullptr;
	int m_nextIcon = 0;
};

} // namespace GuiEditorApp
