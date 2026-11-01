/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

#include "App/Windows/HelpWindow.h"

#include <QFile>
#include <QRegularExpression>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace MapEditorApp
{

HelpWindow::HelpWindow(QWidget *parent) : QDialog(parent), m_browser(new QTextBrowser(this))
{
	setObjectName(QStringLiteral("HelpWindow"));
	setWindowTitle(T("Help"));
	resize(640, 720);
	auto *col = new QVBoxLayout(this);
	m_browser->setOpenLinks(true);
	col->addWidget(m_browser);
}

QString HelpWindow::text(Language language)
{
	QFile file(language == Language::German ? QStringLiteral(":/mapeditor/help/de.html") : QStringLiteral(":/mapeditor/help/en.html"));
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

QStringList HelpWindow::topics(Language language)
{
	QStringList topics;
	static const QRegularExpression HEADING(QStringLiteral("<h2 id=\"([a-z-]+)\">"));
	for (const QRegularExpressionMatch &m : HEADING.globalMatch(text(language)))
		topics << m.captured(1);
	return topics;
}

void HelpWindow::showTopic(Language language, const QString &topic)
{
	m_browser->setHtml(text(language));
	show();
	raise();
	m_browser->scrollToAnchor(topic);
}

} // namespace MapEditorApp
