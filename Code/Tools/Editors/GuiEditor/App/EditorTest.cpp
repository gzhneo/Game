/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// EditorTest.cpp -- the main window of the GUI editor: the test mode. Mouse and keys go to the game's windows
// as in the game; the panel lists what the gadgets report. Leaving the mode reads the layout again, so what
// the test changed (a ticked box, typed text) is gone and the file stays as it was.

#include "App/EditorWindow.h"

#include "App/EditorShared.h"
#include "App/GuiEngine.h"
#include "App/LayoutView.h"
#include "EditorCommon/Language.h"

#include <QAction>
#include <QDockWidget>
#include <QListWidget>

namespace GuiEditorApp
{

using namespace EditorCommon;

namespace
{

constexpr int MAX_LOG = 500;

} // namespace

void EditorWindow::setTestMode(bool on)
{
	if (on == m_testMode || (on && !m_engine))
		return;
	m_testMode = on;
	m_gesture = Gesture::None;
	m_testLog.clear();
	if (on)
	{
		m_view->onTestInput = [this](int kind, int x, int y, int value) { testInput(kind, x, y, value); };
		m_view->setMouseTracking(true);
		(void)GuiEngine::instance().events();
		m_testDock->show();
		m_testDock->raise();
		m_view->setFocus(Qt::OtherFocusReason);
	}
	else
	{
		m_view->onTestInput = nullptr;
		m_view->setMouseTracking(false);
		reloadEngine();
	}
	if (QAction *a = m_actions.value(QStringLiteral("TestMode")))
	{
		const bool blocked = a->blockSignals(true);
		a->setChecked(on);
		a->blockSignals(blocked);
	}
	fillTestLog();
	updateOverlay();
	updateActions();
	updateStatus();
}

bool EditorWindow::testInput(int kind, int x, int y, int value)
{
	if (!m_testMode)
		return false;
	const bool used = GuiEngine::instance().input(kind, x, y, value);
	const QStringList events = GuiEngine::instance().events();
	if (!events.isEmpty())
	{
		for (const QString &e : events)
			m_testLog << eventText(e);
		while (m_testLog.size() > MAX_LOG)
			m_testLog.removeFirst();
		fillTestLog();
	}
	return used;
}

QString EditorWindow::eventText(const QString &event) const
{
	const QStringList parts = event.split(QLatin1Char('\t'));
	const QString window = parts.value(0).section(QLatin1Char(':'), -1);
	const QString message = parts.value(1);
	const QString value = parts.value(2);
	QString what;
	if (message == QLatin1String("GBM_SELECTED"))
		what = value.isEmpty() ? T("pressed") : value == QLatin1String("1") ? T("ticked") : T("unticked");
	else if (message == QLatin1String("GBM_SELECTED_RIGHT"))
		what = T("pressed with the right button");
	else if (message == QLatin1String("GSM_SLIDER_TRACK"))
		what = T("slider at %1").arg(value);
	else if (message == QLatin1String("GSM_SLIDER_DONE"))
		what = T("slider let go at %1").arg(value);
	else if (message == QLatin1String("GLM_SELECTED"))
		what = T("row %1 chosen").arg(value);
	else if (message == QLatin1String("GLM_DOUBLE_CLICKED"))
		what = T("row %1 double-clicked").arg(value);
	else if (message == QLatin1String("GLM_RIGHT_CLICKED"))
		what = T("row %1 clicked with the right button").arg(value);
	else if (message == QLatin1String("GCM_SELECTED"))
		what = T("entry chosen");
	else if (message == QLatin1String("GEM_EDIT_DONE") || message == QLatin1String("GCM_EDIT_DONE"))
		what = T("input finished");
	else if (message == QLatin1String("GEM_UPDATE_TEXT") || message == QLatin1String("GCM_UPDATE_TEXT"))
		what = value.isEmpty() ? T("text changed") : T("text: %1").arg(value);
	else
		what = message;
	return QStringLiteral("%1 \u2013 %2  (%3)").arg(window, what, message);
}

void EditorWindow::fillTestLog()
{
	auto *list = qobject_cast<QListWidget *>(m_testDock->widget());
	if (!list)
		return;
	list->clear();
	if (!m_testMode)
		list->addItem(T("Test mode (Ctrl+T): mouse and keys work the windows as in the game; what they report shows here."));
	else if (m_testLog.isEmpty())
		list->addItem(T("Work the windows with the mouse and the keyboard."));
	else
		list->addItems(m_testLog);
	list->scrollToBottom();
	m_testDock->setWindowTitle(m_testMode ? T("Test (%1)").arg(m_testLog.size()) : T("Test"));
}

QWidget *EditorWindow::buildTestPage()
{
	auto *list = new QListWidget;
	list->setObjectName(QStringLiteral("TestLog"));
	return list;
}

} // namespace GuiEditorApp
