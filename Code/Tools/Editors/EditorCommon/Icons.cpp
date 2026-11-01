/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Icons.cpp -- the editor's line icons.

#include "EditorCommon/Icons.h"

#include <QHash>
#include <QPainter>
#include <QSvgRenderer>

namespace EditorCommon
{

namespace
{

const QHash<QString, QString> &shapes()
{
	static const QHash<QString, QString> s = {
		{"select", R"(<path d="M5 3l14 7-6 2-2 6z"/>)"},
		{"height", R"(<path d="M3 20l6-10 4 6 3-4 5 8z"/>)"},
		{"texture", R"(<path d="M15 4l5 5-8 8-5-5z"/><path d="M7 12l-3 8 8-3"/>)"},
		{"object", R"(<path d="M12 3l8 4.5v9L12 21l-8-4.5v-9z"/><path d="M4 7.5l8 4.5 8-4.5M12 12v9"/>)"},
		{"road", R"(<path d="M8 3L4 21M16 3l4 18M12 5v3M12 11v3M12 17v3"/>)"},
		{"waypoint", R"(<circle cx="6" cy="6" r="2.5"/><circle cx="18" cy="12" r="2.5"/><circle cx="7" cy="18" r="2.5"/><path d="M8.2 7.2l7.6 3.6M15.8 13.2l-6.6 3.6"/>)"},
		{"area", R"(<path d="M5 8l7-5 7 6-3 11H7z"/>)"},
		{"water", R"(<path d="M12 3c4 5 6 8 6 11a6 6 0 01-12 0c0-3 2-6 6-11z"/>)"},
		{"light", R"(<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/>)"},
		{"sun", R"(<circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M5 5l1.5 1.5M17.5 17.5L19 19M5 19l1.5-1.5M17.5 6.5L19 5"/>)"},
		{"moon", R"(<path d="M20 14.5A8 8 0 019.5 4 8 8 0 1020 14.5z"/>)"},
		{"script", R"(<path d="M8 6l-5 6 5 6M16 6l5 6-5 6"/>)"},
		{"layers", R"(<path d="M12 3l9 5-9 5-9-5z"/><path d="M3 13l9 5 9-5"/>)"},
		{"search", R"(<circle cx="11" cy="11" r="7"/><path d="M20 20l-4-4"/>)"},
		{"history", R"(<path d="M3 12a9 9 0 103-6.7L3 8"/><path d="M3 3v5h5M12 7v5l3 2"/>)"},
		{"settings", R"(<path d="M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1"/><circle cx="15" cy="6" r="2"/><circle cx="9" cy="12" r="2"/><circle cx="17" cy="18" r="2"/>)"},
		{"file-new", R"(<path d="M14 3H6v18h12V7z"/><path d="M14 3v4h4M12 11v6M9 14h6"/>)"},
		{"folder", R"(<path d="M3 6a2 2 0 012-2h4l2 2h8a2 2 0 012 2v10a2 2 0 01-2 2H5a2 2 0 01-2-2z"/>)"},
		{"archive", R"(<path d="M3 7h18v4H3zM5 11v9h14v-9M10 15h4"/>)"},
		{"play", R"(<path d="M7 4l13 8-13 8z" fill="currentColor"/>)"},
		{"check", R"(<path d="M5 12l5 5 9-10"/>)"},
		{"close", R"(<path d="M6 6l12 12M18 6L6 18"/>)"},
		{"chevron-down", R"(<path d="M6 9l6 6 6-6"/>)"},
		{"grid", R"(<path d="M3 3h18v18H3zM3 9h18M3 15h18M9 3v18M15 3v18"/>)"},
		{"map", R"(<path d="M3 6l6-3 6 3 6-3v15l-6 3-6-3-6 3z"/><path d="M9 3v15M15 6v15"/>)"},
		{"globe", R"(<circle cx="12" cy="12" r="9"/><path d="M3 12h18M12 3c3 3.5 3 14.5 0 18M12 3c-3 3.5-3 14.5 0 18"/>)"},
		{"info", R"(<circle cx="12" cy="12" r="9"/><path d="M12 11v6M12 7.5v.5"/>)"},
		{"save", R"(<path d="M5 3h11l3 3v15H5z"/><path d="M8 3v5h7V3M8 21v-7h8v7"/>)"},
	};
	return s;
}

QByteArray svg(const QString &name, const QColor &color)
{
	const QString body = shapes().value(name, shapes().value(QStringLiteral("info")));
	QString doc = QStringLiteral(
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\" "
		"stroke-width=\"1.75\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>").arg(color.name(), body);
	doc.replace(QStringLiteral("currentColor"), color.name());
	return doc.toUtf8();
}

} // namespace

QPixmap iconPixmap(const QString &name, const QColor &color, int size, qreal dpr)
{
	QSvgRenderer renderer(svg(name, color));
	QPixmap pm(QSize(size, size) * dpr);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	renderer.render(&p);
	p.end();
	pm.setDevicePixelRatio(dpr);
	return pm;
}

QIcon icon(const QString &name, const QColor &color)
{
	QIcon result;
	for (int size : {16, 20, 24, 32, 48})
		result.addPixmap(iconPixmap(name, color, size));
	return result;
}

} // namespace EditorCommon
