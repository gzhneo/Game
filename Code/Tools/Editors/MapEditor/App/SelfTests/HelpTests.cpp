/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// HelpTests.cpp -- the self test of the help (MapEditor --selftest).

#include "App/MainWindow.h"
#include "App/SelfTest.h"
#include "App/Windows/HelpWindow.h"
#include <QTextBrowser>


namespace MapEditorApp
{

bool MainWindow::selfTestHelp(QStringList &report)
{
	TestSteps step(report, "help");
	const QStringList english = HelpWindow::topics(Language::English);
	const QStringList german = HelpWindow::topics(Language::German);
	step(english.size() >= 20 && english == german, QStringLiteral("both languages have the same %1 topics").arg(english.size()));
	// Every topic F1 can ask for exists.
	const QStringList asked = {QStringLiteral("start"), QStringLiteral("view"), QStringLiteral("objects"), QStringLiteral("waypoints"), QStringLiteral("areas"),
		QStringLiteral("roads"), QStringLiteral("placement"), QStringLiteral("buildlist"), QStringLiteral("settings"), QStringLiteral("terrain-height"),
		QStringLiteral("terrain-texture"), QStringLiteral("terrain-shape"), QStringLiteral("terrain-pathing"), QStringLiteral("scripts"),
		QStringLiteral("sides"), QStringLiteral("particles"), QStringLiteral("problems"), QStringLiteral("history"), QStringLiteral("search"),
		QStringLiteral("fairness"), QStringLiteral("symmetry"), QStringLiteral("poster"), QStringLiteral("layers"), QStringLiteral("shortcuts")};
	QStringList missing;
	for (const QString &topic : asked)
		if (!english.contains(topic))
			missing << topic;
	step(missing.isEmpty(), QStringLiteral("every context has its topic (missing: %1)").arg(missing.join(QStringLiteral(", "))));
	const QString topic = helpTopic();
	showHelp(topic);
	step(m_helpWindow && m_helpWindow->isVisible() && m_helpWindow->browser()->document()->characterCount() > 1000,
		QStringLiteral("F1 opens the help at \"%1\"").arg(topic));
	if (m_helpWindow)
		m_helpWindow->close();
	return step.ok();
}

} // namespace MapEditorApp
