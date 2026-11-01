/*
**	Copyright (C) 2026 GZH Neo
**	Part of GZH Neo, a modified version of Command & Conquer Generals / Zero Hour
**	(Electronic Arts). Licensed under the GNU GPL v3 or later; see LICENSE.md and NOTICE.md.
*/

// MainWindowStart.cpp -- the start page: game, recent maps with their preview pictures, opening and creating maps.

#include "App/MainWindow.h"
#include "App/Common.h"
#include "App/EngineClient.h"
#include "App/Icons.h"
#include "App/Language.h"
#include "App/MapImage.h"
#include "App/MapOverview.h"
#include "App/MiniMap.h"
#include "App/ToolEvent.h"
#include "App/Viewport3D.h"
#include "App/Widgets.h"
#include "Core/SidesCommands.h"
#include "Model/MapDump.h"
#include "Model/MapIO.h"
#include <QButtonGroup>
#include <QDir>
#include <QLocale>
#include <QDirIterator>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

namespace MapEditorApp
{

namespace
{

constexpr int PREVIEW_W = 300;
constexpr int PREVIEW_H = 170;

// Preview and size of a map file, cached by path and modification time.
struct PreviewInfo
{
	QImage image;
	QString size;
	bool ok = false;
};

PreviewInfo preview(const QString &path)
{
	static QHash<QString, std::pair<QDateTime, PreviewInfo>> cache;
	const QFileInfo info(path);
	const QDateTime stamp = info.lastModified();
	const auto it = cache.constFind(path);
	if (it != cache.constEnd() && it->first == stamp)
		return it->second;
	PreviewInfo p;
	if (info.exists())
	{
		const MapModel::LoadedMap loaded = MapModel::loadMap(toFsPath(path));
		if (loaded.result.ok() && loaded.result.map.heightMap)
		{
			MapImageOptions options;
			options.maxSize = 512;
			p.image = renderMapImage(loaded.result.map, options);
			p.size = QStringLiteral("%1 \u00d7 %2").arg(loaded.result.map.heightMap->width).arg(loaded.result.map.heightMap->height);
			p.ok = true;
		}
	}
	cache.insert(path, {stamp, p});
	return p;
}

// Image cropped to fill the card.
QPixmap cardImage(const QImage &source, const Tokens &t)
{
	QPixmap pm(PREVIEW_W, PREVIEW_H);
	pm.fill(t.s3);
	if (!source.isNull())
	{
		QPainter p(&pm);
		const QImage scaled = source.scaled(PREVIEW_W, PREVIEW_H, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
		p.drawImage(QPoint((PREVIEW_W - scaled.width()) / 2, (PREVIEW_H - scaled.height()) / 2), scaled);
	}
	return pm;
}

} // namespace

// Start page

QWidget *MainWindow::buildStartPage()
{
	const Tokens t = tokens(m_state.theme, m_state.game);
	auto *page = new QWidget;
	page->setObjectName(QStringLiteral("Page"));
	auto *row = new QHBoxLayout(page);
	row->setContentsMargins(0, 0, 0, 0);
	row->setSpacing(0);

	// Left column: welcome, game, actions.
	auto *left = new QWidget;
	left->setObjectName(QStringLiteral("Panel"));
	left->setAttribute(Qt::WA_StyledBackground);
	left->setFixedWidth(440);
	auto *col = new QVBoxLayout(left);
	col->setContentsMargins(40, 44, 40, 28);
	col->setSpacing(10);
	col->addWidget(label(T("Welcome"), "Title"));
	auto *intro = label(T("Build and check maps for Generals and Zero Hour."), "Secondary");
	intro->setWordWrap(true);
	col->addWidget(intro);
	col->addSpacing(22);
	col->addWidget(label(T("GAME"), "Section"));

	auto *tiles = new QHBoxLayout;
	tiles->setSpacing(12);
	auto *tileGroup = new QButtonGroup(left);
	const std::pair<Game, QString> games[] = {{Game::Generals, QStringLiteral("Command & Conquer Generals")},
		{Game::ZeroHour, T("Generals \u2013 Zero Hour")}};
	for (const auto &[g, subtitle] : games)
	{
		auto *tile = new QPushButton;
		tile->setObjectName(QStringLiteral("GameTile"));
		tile->setCheckable(true);
		tile->setChecked(g == m_state.game);
		tile->setCursor(Qt::PointingHandCursor);
		tile->setMinimumHeight(118);
		auto *tl = new QVBoxLayout(tile);
		tl->setContentsMargins(16, 14, 16, 14);
		tl->setSpacing(6);
		auto *name = label(gameName(g), "Heading");
		name->setAttribute(Qt::WA_TransparentForMouseEvents);
		auto *sub = label(subtitle, "Hint");
		sub->setWordWrap(true);
		sub->setAttribute(Qt::WA_TransparentForMouseEvents);
		tl->addWidget(name);
		tl->addWidget(sub);
		tl->addStretch(1);
		auto *folder = label(QDir(m_state.userMapFolder(g)).exists() ? T("User map folder found") : T("No user map folder yet"), "Meta");
		folder->setWordWrap(true);
		folder->setAttribute(Qt::WA_TransparentForMouseEvents);
		tl->addWidget(folder);
		tileGroup->addButton(tile);
		const Game chosen = g;
		connect(tile, &QPushButton::clicked, this, [this, chosen] {
			if (m_state.game == chosen)
				return;
			m_state.game = chosen;
			m_state.save();
			QTimer::singleShot(0, this, [this] { applySettings(); });
		});
		tiles->addWidget(tile);
	}
	col->addLayout(tiles);
	auto *gameHint = label(T("The accent colour shows the selected game. Opening a map from a game's folder selects the game."), "Hint");
	gameHint->setWordWrap(true);
	col->addWidget(gameHint);
	col->addSpacing(22);
	col->addWidget(label(T("START"), "Section"));

	auto action = [&](const QString &iconName, const QString &title, const QString &sub, const QString &keys, bool enabled) {
		auto *b = new QPushButton;
		b->setObjectName(QStringLiteral("Action"));
		b->setCursor(Qt::PointingHandCursor);
		b->setEnabled(enabled);
		b->setMinimumHeight(60);
		auto *bl = new QHBoxLayout(b);
		bl->setContentsMargins(12, 8, 12, 8);
		bl->setSpacing(12);
		auto *ic = new QLabel;
		ic->setPixmap(iconPixmap(iconName, enabled ? t.accFg : t.tx3, 20));
		ic->setFixedSize(36, 36);
		ic->setAlignment(Qt::AlignCenter);
		ic->setStyleSheet(QStringLiteral("background: %1; border-radius: 8px;").arg(enabled ? QStringLiteral("rgba(%1,%2,%3,40)").arg(t.accFg.red()).arg(t.accFg.green()).arg(t.accFg.blue()) : t.s3.name()));
		auto *texts = new QVBoxLayout;
		texts->setSpacing(1);
		auto *tt = label(title, nullptr);
		tt->setStyleSheet(QStringLiteral("font-weight: 600;"));
		texts->addWidget(tt);
		auto *subLabel = label(sub, "Meta");
		subLabel->setWordWrap(true);
		texts->addWidget(subLabel);
		bl->addWidget(ic);
		bl->addLayout(texts, 1);
		if (!keys.isEmpty())
			bl->addWidget(label(keys, "Meta"));
		for (QWidget *w : b->findChildren<QWidget *>())
			w->setAttribute(Qt::WA_TransparentForMouseEvents);
		col->addWidget(b);
		return b;
	};
	auto *create = action(QStringLiteral("file-new"), T("New map"), T("Flat terrain with one texture, from the game data"), QStringLiteral("Ctrl+N"), true);
	connect(create, &QPushButton::clicked, this, [this] { newMapDialog(); });
	auto *open = action(QStringLiteral("folder"), T("Open map"), T("From a map folder or any file"), QStringLiteral("Ctrl+O"), true);
	connect(open, &QPushButton::clicked, this, [this] { openDialog(); });
	const AutosaveInfo pending = autosaveInfo();
	if (pending.pending)
	{
		auto *recover = action(QStringLiteral("folder"), T("Recover unsaved work"),
			T("%1, autosaved %2").arg(pending.map.isEmpty() ? T("Unnamed map") : QFileInfo(pending.map).completeBaseName(),
				QLocale().toString(pending.time, QLocale::ShortFormat)), QString(), true);
		recover->setObjectName(QStringLiteral("RecoverAutosave"));
		connect(recover, &QPushButton::clicked, this, [this] { recoverAutosave(); });
	}
	col->addStretch(1);
	auto *tip = label(T("Tip: Ctrl+Shift+P opens the command search."), "Hint");
	tip->setWordWrap(true);
	col->addWidget(tip);
	row->addWidget(left);

	// Right: recent maps and the user's maps as cards.
	auto *scroll = new QScrollArea;
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget;
	content->setObjectName(QStringLiteral("Page"));
	auto *rc = new QVBoxLayout(content);
	rc->setContentsMargins(44, 44, 44, 32);
	rc->setSpacing(16);

	auto *head = new QHBoxLayout;
	head->addWidget(label(T("Recently opened"), "Heading"), 1);
	auto *filterGroup = new QButtonGroup(content);
	struct Card
	{
		QWidget *widget;
		QString name;
		Game game;
	};
	auto cards = std::make_shared<std::vector<Card>>();
	auto *empty = label(QString(), "Hint");
	empty->setAlignment(Qt::AlignCenter);
	empty->setMinimumHeight(120);
	empty->setStyleSheet(QStringLiteral("border: 1px dashed %1; border-radius: 10px;").arg(t.bd.name()));
	auto applyFilter = [this, cards, empty] {
		int shown = 0;
		for (const Card &c : *cards)
		{
			const bool gameOk = m_recentFilter == QStringLiteral("all") || (m_recentFilter == QStringLiteral("generals")) == (c.game == Game::Generals);
			const bool textOk = m_recentQuery.isEmpty() || c.name.contains(m_recentQuery, Qt::CaseInsensitive);
			c.widget->setVisible(gameOk && textOk);
			shown += (gameOk && textOk) ? 1 : 0;
		}
		empty->setVisible(shown == 0);
		empty->setText(cards->empty() ? T("No maps yet. Open a map to see it here.") : T("No map matches the filter."));
	};
	const std::pair<QString, QString> filters[] = {{QStringLiteral("all"), T("All")}, {QStringLiteral("generals"), QStringLiteral("Generals")}, {QStringLiteral("zh"), QStringLiteral("Zero Hour")}};
	for (const auto &[key, text] : filters)
	{
		auto *chip = new QPushButton(text);
		chip->setObjectName(QStringLiteral("Chip"));
		chip->setCheckable(true);
		chip->setChecked(m_recentFilter == key);
		chip->setCursor(Qt::PointingHandCursor);
		filterGroup->addButton(chip);
		const QString k = key;
		connect(chip, &QPushButton::clicked, this, [this, k, applyFilter] {
			m_recentFilter = k;
			applyFilter();
		});
		head->addWidget(chip);
	}
	auto *searchField = new QLineEdit;
	searchField->setPlaceholderText(T("Search maps"));
	searchField->setClearButtonEnabled(true);
	searchField->setFixedWidth(240);
	searchField->addAction(icon(QStringLiteral("search"), t.tx3), QLineEdit::LeadingPosition);
	searchField->setText(m_recentQuery);
	connect(searchField, &QLineEdit::textChanged, this, [this, applyFilter](const QString &text) {
		m_recentQuery = text;
		applyFilter();
	});
	head->addSpacing(8);
	head->addWidget(searchField);
	rc->addLayout(head);

	// Recent maps first, then maps in the user folder of the selected game that are not recent.
	std::vector<RecentMap> entries = m_state.recent;
	const QString folder = m_state.userMapFolder(m_state.game);
	if (QDir(folder).exists())
	{
		QDirIterator it(folder, {QStringLiteral("*.map")}, QDir::Files, QDirIterator::Subdirectories);
		int found = 0;
		while (it.hasNext() && found < 48)
		{
			const QString path = QDir::cleanPath(it.next());
			const bool known = std::any_of(entries.begin(), entries.end(), [&](const RecentMap &r) { return QDir::cleanPath(r.path) == path; });
			if (!known)
			{
				entries.push_back(RecentMap{path, m_state.game, QFileInfo(path).lastModified()});
				++found;
			}
		}
	}

	auto *grid = new QGridLayout;
	grid->setSpacing(20);
	const int columns = 3;
	for (std::size_t i = 0; i < entries.size(); ++i)
	{
		const RecentMap &r = entries[i];
		const PreviewInfo info = preview(r.path);
		auto *card = new QPushButton;
		card->setObjectName(QStringLiteral("Card"));
		card->setCursor(Qt::PointingHandCursor);
		card->setEnabled(info.ok);
		card->setFixedHeight(PREVIEW_H + 64);
		card->setMinimumWidth(240);
		auto *cl = new QVBoxLayout(card);
		cl->setContentsMargins(1, 1, 1, 10);
		cl->setSpacing(4);
		auto *img = new QLabel;
		img->setPixmap(cardImage(info.image, t));
		img->setScaledContents(true);
		img->setFixedHeight(PREVIEW_H);
		img->setStyleSheet(QStringLiteral("border-top-left-radius: 9px; border-top-right-radius: 9px;"));
		cl->addWidget(img);
		auto *line = new QHBoxLayout;
		line->setContentsMargins(14, 4, 14, 0);
		const QString name = QFileInfo(r.path).completeBaseName();
		auto *title = label(name, nullptr);
		title->setStyleSheet(QStringLiteral("font-weight: 600;"));
		line->addWidget(title, 1);
		line->addWidget(label(gameName(r.game), "Pill"));
		cl->addLayout(line);
		QString meta = info.ok ? info.size : T("File missing or unreadable");
		const QString when = relativeTime(r.opened);
		if (!when.isEmpty())
			meta += QStringLiteral(" \u00b7 ") + when;
		auto *metaLabel = label(meta, "Meta");
		metaLabel->setContentsMargins(14, 0, 14, 0);
		cl->addWidget(metaLabel);
		for (QWidget *w : card->findChildren<QWidget *>())
			w->setAttribute(Qt::WA_TransparentForMouseEvents);
		const QString path = r.path;
		connect(card, &QPushButton::clicked, this, [this, path] {
			QTimer::singleShot(0, this, [this, path] { openMapFile(path); });
		});
		grid->addWidget(card, static_cast<int>(i) / columns, static_cast<int>(i) % columns);
		cards->push_back(Card{card, name, r.game});
	}
	for (int c = 0; c < columns; ++c)
		grid->setColumnStretch(c, 1);
	rc->addLayout(grid);
	rc->addWidget(empty);
	rc->addStretch(1);
	applyFilter();
	scroll->setWidget(content);
	row->addWidget(scroll, 1);
	return page;
}

} // namespace MapEditorApp
