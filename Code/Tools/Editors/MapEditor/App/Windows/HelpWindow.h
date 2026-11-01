/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HelpWindow.h -- the built-in help (Resources/Help: one HTML file per language, one section per topic,
// <h2 id="topic">), opened at a topic.

#pragma once

#include "App/Language.h"

#include <QDialog>
#include <QStringList>

class QTextBrowser;

namespace MapEditorApp
{

class HelpWindow : public QDialog
{
	Q_OBJECT

public:
	explicit HelpWindow(QWidget *parent);

	void showTopic(Language language, const QString &topic);
	[[nodiscard]] QTextBrowser *browser() const { return m_browser; }

	[[nodiscard]] static QString text(Language language);
	// The topics of the help in the order of the file.
	[[nodiscard]] static QStringList topics(Language language);

private:
	QTextBrowser *m_browser;
};

} // namespace MapEditorApp
