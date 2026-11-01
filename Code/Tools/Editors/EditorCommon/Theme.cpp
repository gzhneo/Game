/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// Theme.cpp -- design tokens and their translation into a Qt palette and style sheet.

#include "EditorCommon/Theme.h"

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>

namespace EditorCommon
{

Tokens tokens(ThemeKind theme, Game game)
{
	Tokens t;
	if (theme == ThemeKind::Dark)
	{
		t.bg = QColor(0x14, 0x15, 0x18);
		t.s1 = QColor(0x1b, 0x1d, 0x21);
		t.s2 = QColor(0x21, 0x24, 0x29);
		t.s3 = QColor(0x2b, 0x2f, 0x35);
		t.bd = QColor(0x30, 0x34, 0x3b);
		t.tx = QColor(0xe9, 0xea, 0xed);
		t.tx2 = QColor(0xaa, 0xb0, 0xba);
		t.tx3 = QColor(0x8e, 0x94, 0x9f);
		t.error = QColor(0xff, 0x6b, 0x6b);
	}
	else
	{
		t.bg = QColor(0xec, 0xee, 0xf1);
		t.s1 = QColor(0xff, 0xff, 0xff);
		t.s2 = QColor(0xf5, 0xf6, 0xf8);
		t.s3 = QColor(0xe7, 0xea, 0xee);
		t.bd = QColor(0xd7, 0xdb, 0xe1);
		t.tx = QColor(0x15, 0x17, 0x1b);
		t.tx2 = QColor(0x45, 0x4a, 0x54);
		t.tx3 = QColor(0x5c, 0x62, 0x6e);
		t.error = QColor(0xc6, 0x2a, 0x2f);
	}
	const bool dark = theme == ThemeKind::Dark;
	if (game == Game::Generals)
	{
		t.acc = QColor(0x2f, 0x6f, 0xe0);
		t.accFg = dark ? QColor(0x72, 0xa5, 0xff) : QColor(0x1f, 0x5b, 0xc7);
	}
	else
	{
		t.acc = QColor(0xb8, 0x52, 0x0e);
		t.accFg = dark ? QColor(0xff, 0xa0, 0x4d) : QColor(0x9c, 0x44, 0x09);
	}
	t.accBg = t.accFg;
	t.accBg.setAlphaF(dark ? 0.16f : 0.11f);
	t.axisX = QColor(0xe5, 0x48, 0x4d);
	t.axisY = QColor(0x46, 0xa7, 0x58);
	t.axisZ = QColor(0x3d, 0x8b, 0xfd);
	return t;
}

QString gameName(Game game)
{
	return game == Game::Generals ? QStringLiteral("Generals") : QStringLiteral("Zero Hour");
}

namespace
{

QString rgba(const QColor &c)
{
	return QStringLiteral("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alphaF(), 0, 'f', 3);
}

// Object names used by the style sheet: panels, tiles and buttons set them, colours stay here.
QString styleSheet(const Tokens &t)
{
	QString s = QStringLiteral(R"(
QWidget { color: @tx; font-size: 13px; }
QMainWindow, QStackedWidget#Pages, QWidget#Page { background: @bg; }
QWidget#Panel { background: @s1; }
QWidget#TopBar { background: @s1; border-bottom: 1px solid @bd; }
QStatusBar { background: @s1; border-top: 1px solid @bd; color: @tx3; font-size: 12px; }
QStatusBar QLabel { color: @tx3; font-size: 12px; padding: 0 8px; }
QLabel#Title { font-size: 28px; font-weight: 600; }
QLabel#Heading { font-size: 15px; font-weight: 600; }
QLabel#Section { font-size: 11px; font-weight: 600; color: @tx3; letter-spacing: 1px; }
QLabel#Hint, QLabel#Meta { color: @tx3; font-size: 12px; }
QLabel#Secondary { color: @tx2; }
QLabel#Badge { color: @tx2; font-size: 12px; }
QLabel#Pill { background: @s3; color: @tx2; border-radius: 9px; padding: 1px 8px; font-size: 11px; }
QMenuBar { background: transparent; color: @tx2; padding: 0; margin: 0; }
QMenuBar::item { padding: 4px 8px; border-radius: 5px; background: transparent; }
QMenuBar::item:selected { background: @s3; color: @tx; }
QMenu { background: @s1; border: 1px solid @bd; padding: 4px; }
QMenu::item { padding: 6px 24px 6px 12px; border-radius: 5px; }
QMenu::item:selected { background: @s3; }
QMenu::item:disabled { color: @tx3; }
QMenu::separator { height: 1px; background: @bd; margin: 4px 6px; }
QPushButton { background: @s3; border: 1px solid @bd; border-radius: 6px; padding: 6px 14px; }
QPushButton:hover { border-color: @tx3; }
QPushButton:disabled { color: @tx3; }
QPushButton#Primary { background: @acc; border: none; color: #ffffff; font-weight: 600; }
QPushButton#Primary:hover { background: @accHover; }
QPushButton#Primary:disabled { background: @s3; color: @tx3; font-weight: 600; }
QPushButton#Ghost { background: transparent; border: none; color: @tx2; text-align: left; }
QPushButton#Ghost:hover { background: @s3; color: @tx; }
QPushButton#Chip { background: transparent; border: 1px solid @bd; border-radius: 11px; padding: 3px 12px; min-height: 18px; color: @tx2; font-size: 12px; }
QPushButton#Chip:hover { border-color: @tx3; color: @tx; }
QPushButton#Chip:checked { background: @accBg; border-color: @accFg; color: @accFg; }
QPushButton#Segment { background: transparent; border: none; border-radius: 5px; padding: 4px 12px; color: @tx2; font-size: 12px; }
QPushButton#Segment:hover { color: @tx; }
QPushButton#Segment:checked { background: @s3; color: @tx; }
QPushButton#Segment:disabled { color: @tx3; }
QWidget#SegmentGroup { background: @s2; border: 1px solid @bd; border-radius: 7px; }
QPushButton#GameTile { background: @s1; border: 1px solid @bd; border-radius: 12px; padding: 18px; text-align: left; }
QPushButton#GameTile:hover { border-color: @tx3; }
QPushButton#GameTile:checked { border: 2px solid @accFg; }
QPushButton#Card { background: @s1; border: 1px solid @bd; border-radius: 10px; padding: 0; }
QPushButton#Card:hover { border-color: @accFg; }
QPushButton#Action { background: transparent; border: none; border-radius: 8px; padding: 10px 12px; text-align: left; }
QPushButton#Action:hover { background: @s3; }
QPushButton#Action:disabled { color: @tx3; }
QLineEdit, QComboBox, QDoubleSpinBox { background: @s2; border: 1px solid @bd; border-radius: 6px; padding: 5px 8px; selection-background-color: @acc; }
QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { border-color: @accFg; }
QComboBox::drop-down { border: none; width: 20px; }
QComboBox QAbstractItemView { background: @s1; border: 1px solid @bd; selection-background-color: @s3; }
QTreeWidget, QListWidget, #HistoryList, #SearchResults, #ProblemList { background: @s1; border: none; outline: none; }
QTreeWidget::item, QListWidget::item, #HistoryList::item, #SearchResults::item, #ProblemList::item { padding: 4px; border-radius: 5px; }
QTreeWidget::item:hover, QListWidget::item:hover, #HistoryList::item:hover, #SearchResults::item:hover, #ProblemList::item:hover { background: @s3; }
QTreeWidget::item:selected, QListWidget::item:selected, #HistoryList::item:selected, #SearchResults::item:selected, #ProblemList::item:selected { background: @accBg; color: @tx; }
QHeaderView::section { background: @s1; color: @tx3; border: none; padding: 4px; font-size: 11px; }
QScrollArea { background: transparent; border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @s3; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @bd; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @s3; border-radius: 4px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QToolTip { background: @s1; color: @tx; border: 1px solid @bd; padding: 4px 8px; }
QSplitter::handle { background: @bd; }
QFrame#Line { background: @bd; max-height: 1px; min-height: 1px; }
QFrame#VLine { background: @bd; max-width: 1px; min-width: 1px; }
)");
	QColor hover = t.acc.lighter(112);
	s.replace(QStringLiteral("@accHover"), hover.name());
	s.replace(QStringLiteral("@accBg"), rgba(t.accBg));
	s.replace(QStringLiteral("@accFg"), t.accFg.name());
	s.replace(QStringLiteral("@acc"), t.acc.name());
	s.replace(QStringLiteral("@bg"), t.bg.name());
	s.replace(QStringLiteral("@s1"), t.s1.name());
	s.replace(QStringLiteral("@s2"), t.s2.name());
	s.replace(QStringLiteral("@s3"), t.s3.name());
	s.replace(QStringLiteral("@bd"), t.bd.name());
	s.replace(QStringLiteral("@tx2"), t.tx2.name());
	s.replace(QStringLiteral("@tx3"), t.tx3.name());
	s.replace(QStringLiteral("@tx"), t.tx.name());
	return s;
}

// Fusion draws the frame of check boxes and radio buttons in a shade of the window colour, which
// vanishes on the dark theme; the indicators are drawn from the tokens instead.
class ThemeStyle : public QProxyStyle
{
public:
	explicit ThemeStyle(const Tokens &t) : QProxyStyle(QStyleFactory::create(QStringLiteral("Fusion"))), m_t(t) {}

	void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override
	{
		const bool check = element == PE_IndicatorCheckBox || element == PE_IndicatorItemViewItemCheck;
		if (!check && element != PE_IndicatorRadioButton)
		{
			QProxyStyle::drawPrimitive(element, option, painter, widget);
			return;
		}
		const bool enabled = option->state & State_Enabled;
		const bool on = option->state & State_On;
		const bool partly = option->state & State_NoChange;
		const bool hover = enabled && (option->state & State_MouseOver);
		const int side = qMin(option->rect.width(), option->rect.height()) - 2;
		const QRectF box(option->rect.x() + (option->rect.width() - side) / 2.0 + 0.5, option->rect.y() + (option->rect.height() - side) / 2.0 + 0.5, side - 1,
			side - 1);
		const bool filled = enabled && (on || partly);
		const QColor border = !enabled ? m_t.bd : filled ? m_t.acc : hover ? m_t.accFg : m_t.tx3;
		painter->save();
		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(QPen(border, 1.2));
		painter->setBrush(filled ? m_t.acc : m_t.s2);
		if (check)
			painter->drawRoundedRect(box, 3, 3);
		else
			painter->drawEllipse(box);
		const QColor mark = enabled ? QColor(Qt::white) : m_t.tx3;
		if (!check && on)
		{
			painter->setPen(Qt::NoPen);
			painter->setBrush(mark);
			painter->drawEllipse(box.center(), side * 0.2, side * 0.2);
		}
		else if (check && partly)
		{
			painter->setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap));
			painter->drawLine(QPointF(box.left() + side * 0.25, box.center().y()), QPointF(box.right() - side * 0.25, box.center().y()));
		}
		else if (check && on)
		{
			QPainterPath tick;
			tick.moveTo(box.left() + side * 0.22, box.top() + side * 0.52);
			tick.lineTo(box.left() + side * 0.42, box.top() + side * 0.72);
			tick.lineTo(box.left() + side * 0.78, box.top() + side * 0.30);
			painter->setPen(QPen(mark, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			painter->setBrush(Qt::NoBrush);
			painter->drawPath(tick);
		}
		painter->restore();
	}

private:
	Tokens m_t;
};

} // namespace

void applyTheme(QApplication &app, ThemeKind theme, Game game)
{
	const Tokens t = tokens(theme, game);
	app.setStyle(new ThemeStyle(t));

	QPalette p;
	p.setColor(QPalette::Window, t.bg);
	p.setColor(QPalette::WindowText, t.tx);
	p.setColor(QPalette::Base, t.s2);
	p.setColor(QPalette::AlternateBase, t.s1);
	p.setColor(QPalette::Text, t.tx);
	p.setColor(QPalette::PlaceholderText, t.tx3);
	p.setColor(QPalette::Button, t.s3);
	p.setColor(QPalette::ButtonText, t.tx);
	p.setColor(QPalette::Highlight, t.acc);
	p.setColor(QPalette::HighlightedText, QColor(Qt::white));
	p.setColor(QPalette::ToolTipBase, t.s1);
	p.setColor(QPalette::ToolTipText, t.tx);
	p.setColor(QPalette::Link, t.accFg);
	p.setColor(QPalette::Mid, t.bd);
	p.setColor(QPalette::Dark, t.bd);
	p.setColor(QPalette::Disabled, QPalette::Text, t.tx3);
	p.setColor(QPalette::Disabled, QPalette::ButtonText, t.tx3);
	p.setColor(QPalette::Disabled, QPalette::WindowText, t.tx3);
	app.setPalette(p);

	QFont font(QStringLiteral("Segoe UI Variable Text"));
	font.setFamilies({QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"), QStringLiteral("Tahoma")});
	font.setPixelSize(13);
	app.setFont(font);
	app.setStyleSheet(styleSheet(t));
}

} // namespace EditorCommon
