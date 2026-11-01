/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// SelfTest.h -- the report lines of the editors' self tests (--selftest): one line per
// step, "ok   <area>: <what>" or "FAIL <area>: <what>".

#pragma once

#include <QString>
#include <QStringList>

namespace EditorCommon
{

class TestSteps
{
public:
	TestSteps(QStringList &report, const char *area) : m_report(report), m_area(QString::fromLatin1(area)) {}

	// Adds the line of one step; returns passed.
	bool operator()(bool passed, const QString &what)
	{
		m_report << (passed ? QStringLiteral("ok   ") : QStringLiteral("FAIL ")) + m_area + QStringLiteral(": ") + what;
		m_ok = m_ok && passed;
		return passed;
	}
	// Folds in a part test that wrote its own lines.
	void add(bool passed) { m_ok = m_ok && passed; }
	[[nodiscard]] bool ok() const { return m_ok; }

private:
	QStringList &m_report;
	QString m_area;
	bool m_ok = true;
};

} // namespace EditorCommon
