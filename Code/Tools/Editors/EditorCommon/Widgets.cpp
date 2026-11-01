/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Widgets.cpp -- small building blocks with the object names the style sheet knows.

#include "EditorCommon/Widgets.h"

#include "EditorCommon/Language.h"

#include <QButtonGroup>
#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>

namespace EditorCommon
{

QLabel *label(const QString &text, const char *objectName, QWidget *parent)
{
	auto *l = new QLabel(text, parent);
	if (objectName)
		l->setObjectName(QString::fromLatin1(objectName));
	return l;
}

QWidget *hLine(QWidget *parent)
{
	auto *f = new QFrame(parent);
	f->setObjectName(QStringLiteral("Line"));
	return f;
}

QWidget *segmented(const std::vector<QString> &items, int current, const std::function<void(int)> &onPick,
	QWidget *parent, const std::vector<bool> &enabled)
{
	auto *group = new QWidget(parent);
	group->setObjectName(QStringLiteral("SegmentGroup"));
	group->setAttribute(Qt::WA_StyledBackground);
	auto *row = new QHBoxLayout(group);
	row->setContentsMargins(2, 2, 2, 2);
	row->setSpacing(0);
	auto *buttons = new QButtonGroup(group);
	for (std::size_t i = 0; i < items.size(); ++i)
	{
		auto *b = new QPushButton(items[i], group);
		b->setObjectName(QStringLiteral("Segment"));
		b->setCheckable(true);
		b->setCursor(Qt::PointingHandCursor);
		b->setChecked(static_cast<int>(i) == current);
		if (i < enabled.size() && !enabled[i])
			b->setEnabled(false);
		buttons->addButton(b, static_cast<int>(i));
		row->addWidget(b);
	}
	QObject::connect(buttons, &QButtonGroup::idClicked, group, [onPick](int id) {
		if (onPick)
			onPick(id);
	});
	return group;
}

QString relativeTime(const QDateTime &when)
{
	if (!when.isValid())
		return QString();
	const qint64 secs = when.secsTo(QDateTime::currentDateTime());
	if (secs < 90)
		return T("just now");
	if (secs < 3600)
		return T("%1 minutes ago").arg(secs / 60);
	if (secs < 2 * 3600)
		return T("1 hour ago");
	if (secs < 24 * 3600)
		return T("%1 hours ago").arg(secs / 3600);
	if (secs < 2 * 24 * 3600)
		return T("yesterday");
	if (secs < 14 * 24 * 3600)
		return T("%1 days ago").arg(secs / (24 * 3600));
	return QLocale().toString(when.date(), QLocale::ShortFormat);
}

} // namespace EditorCommon
